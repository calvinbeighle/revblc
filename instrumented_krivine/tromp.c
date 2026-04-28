/*
 * tromp.c - reversible instrumentation of the BLC/Krivine core.
 *
 * This is the "literal ask" work area: a standalone, un-golfed C derivative
 * of the transition structure in John Tromp's IOCCC 2012 lambda interpreter.
 * The original pasted source is kept at:
 *
 *   ../tromp-krivine-ioccc2012.c
 *
 * The IOCCC expression is intentionally not preserved here. The point of this
 * file is to expose every mutable operation, log the old value, run forward,
 * then consume the log backward and prove that the post-parse, pre-reduction
 * state is restored byte-for-byte.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rlog.h"

#define ARENA_WORDS 500000L
#define HEAP_BASE 250000L
#define CELL_WORDS 4L
#define RLOG_CAP 2000000L
#define IO_CAP 1048576L
#define DEFAULT_MAX_STEPS 100000L

#define IOP 0L
#define VAR 1L
#define APP 2L
#define ABS 3L

#define NIL 23L
#define FALSE 23L
#define TRUE 27L
#define WR0 20L
#define WR1 21L
#define KROM_LAST_IOP WR1

#define STATIC_ASSERT(name, expr) typedef char static_assert_##name[(expr) ? 1 : -1]

#define CELL_NEXT(p) ((p) + 0L)
#define CELL_REFS(p) ((p) + 1L)
#define CELL_ENV(p)  ((p) + 2L)
#define CELL_TERM(p) ((p) + 3L)

static const long kRom[] = {
    APP, 0,       /* 0: patched to call parsed main with input thunk */
    ABS,          /* 2 */
    APP, 0,       /* 3: patched by mode */
    VAR, 0,       /* 5 */
    ABS,          /* 7 */
    APP,          /* 8 */
    ABS,          /* 9 */
    APP, 2,       /* 10 */
    VAR,          /* 12 */
    IOP,          /* 13 */
    ABS,          /* 14 */
    APP, 4,       /* 15 */
    APP, 1,       /* 17 */
    VAR,          /* 19 */
    IOP,          /* 20: wr0 */
    IOP, 0,       /* 21: wr1 */
    ABS,          /* 23: nil / false */
    ABS,          /* 24: exit */
    VAR, 0,       /* 25 */
    ABS,          /* 27: true */
    ABS,          /* 28 */
    VAR, 1        /* 29 */
};

STATIC_ASSERT(krom_len_is_31, sizeof(kRom) / sizeof(kRom[0]) == 31);

RLogEntry RLOG[RLOG_CAP];
long RLOG_LEN;

static long L[ARENA_WORDS];
static long L_SNAPSHOT[ARENA_WORDS];

/*
 * Names mirror the IOCCC globals where possible:
 *   a = current environment pointer
 *   C = current term/code pointer
 *   c = continuation/argument stack pointer
 *   D = scratch allocation/walk pointer
 *   U = code end pointer
 *   B = free-list pointer
 *   b,m,u,I,O = bit/input/output scratch state
 *
 * H is added by this readable version as the fresh heap frontier.
 */
static long a, C, c, D, U, B, b, m, u, H, I, O, co;
static long snap_a, snap_C, snap_c, snap_D, snap_U, snap_B;
static long snap_b, snap_m, snap_u, snap_H, snap_I, snap_O, snap_co;

static unsigned char INPUT[IO_CAP];
static unsigned char OUT[IO_CAP];
static long INPUT_LEN;
static long INPUT_POS;
static long OUT_LEN;
static long kLazy[256];
static long kLazy_snapshot[256];

static int halted;
static int exit_code;
static int trace;
static int use_prelude = 1;

typedef struct Parser {
    char *bits;
    size_t len;
    size_t pos;
} Parser;

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        die("out of memory");
    }
    return p;
}

void rlog_push(int tag, long addr, long old) {
    if (RLOG_LEN >= RLOG_CAP) {
        die("residual log exhausted");
    }
    RLOG[RLOG_LEN].tag = tag;
    RLOG[RLOG_LEN].addr = addr;
    RLOG[RLOG_LEN].old = old;
    RLOG_LEN++;
}

static void heap_write(long idx, long val) {
    if (idx < 0 || idx >= ARENA_WORDS) {
        die("heap write out of bounds");
    }
    LOG_HEAP(idx, L[idx]);
    L[idx] = val;
}

static void scalar_write(long *slot, int tag, long val) {
    LOG_SCALAR(tag, *slot);
    *slot = val;
}

static void write_a(long val) { scalar_write(&a, TAG_A, val); }
static void write_C(long val) { scalar_write(&C, TAG_C, val); }
static void write_c(long val) { scalar_write(&c, TAG_c, val); }
static void write_D(long val) { scalar_write(&D, TAG_D, val); }
static void write_B(long val) { scalar_write(&B, TAG_B, val); }
static void write_u(long val) { scalar_write(&u, TAG_u, val); }
static void write_H(long val) { scalar_write(&H, TAG_H, val); }
static void write_I(long val) { scalar_write(&I, TAG_I, val); }
static void write_O(long val) { scalar_write(&O, TAG_O, val); }
static void write_co(long val) { scalar_write(&co, TAG_co, val); }

static void ref_inc(long cell) {
    if (cell) {
        heap_write(CELL_REFS(cell), L[CELL_REFS(cell)] + 1L);
    }
}

static void ref_dec_deferred(long cell) {
    if (cell) {
        rlog_push(TAG_REFCNT_DEC, cell, L[CELL_REFS(cell)]);
        L[CELL_REFS(cell)]--;
        if (L[CELL_REFS(cell)] < 0) {
            die("negative refcount");
        }
    }
}

static long alloc_cell(void) {
    long cell;

    if (B) {
        cell = B;
        rlog_push(TAG_ALLOC_POP, cell, B);
        write_D(cell);
        write_B(L[CELL_NEXT(cell)]);
        return cell;
    }

    if (H + CELL_WORDS >= ARENA_WORDS) {
        die("heap exhausted");
    }
    cell = H;
    rlog_push(TAG_ALLOC_FRESH, cell, H);
    write_D(cell);
    write_H(H + CELL_WORDS);
    return cell;
}

static int bit_at(Parser *p) {
    if (p->pos >= p->len) {
        return -1;
    }
    return p->bits[p->pos++] == '1';
}

static long backref(long target) {
    return target - (U + 1);
}

static void append_code(long val) {
    if (U >= HEAP_BASE) {
        die("code arena exhausted");
    }
    L[U++] = val;
}

static void append_runtime(long val) {
    if (U >= HEAP_BASE) {
        die("code arena exhausted");
    }
    L[U++] = val;
}

static long parse_term(Parser *p) {
    int bit;
    int bit2;
    int ones;
    long start;
    long offset_slot;
    long arg;
    long body;

    start = U;
    bit = bit_at(p);
    if (bit < 0) {
        die("unexpected end of BLC input");
    }
    if (!bit) {
        bit2 = bit_at(p);
        if (bit2 < 0) {
            die("incomplete BLC tag");
        }
        if (!bit2) {
            append_code(ABS);
            body = parse_term(p);
            (void)body;
            return start;
        }
        append_code(APP);
        offset_slot = U;
        append_code(0);
        parse_term(p);
        arg = parse_term(p);
        L[offset_slot] = arg - (start + 2);
        return start;
    }

    ones = 1;
    while ((bit = bit_at(p)) == 1) {
        ones++;
    }
    if (bit < 0) {
        die("unterminated BLC variable");
    }
    append_code(VAR);
    append_code((long)(ones - 1));
    return start;
}

static unsigned char *read_blob(FILE *f, long *out_len) {
    size_t cap = 256;
    size_t len = 0;
    int ch;
    unsigned char *buf = (unsigned char *)xmalloc(cap);

    while ((ch = fgetc(f)) != EOF) {
        if (len >= cap) {
            cap *= 2;
            buf = (unsigned char *)realloc(buf, cap);
            if (!buf) {
                die("out of memory");
            }
        }
        buf[len++] = (unsigned char)ch;
    }
    *out_len = (long)len;
    return buf;
}

static char *clean_bits(const char *raw) {
    size_t cap = strlen(raw) + 1;
    char *bits = (char *)xmalloc(cap);
    size_t i;
    size_t len = 0;

    for (i = 0; raw[i]; i++) {
        unsigned char ch = (unsigned char)raw[i];
        if (ch == '0' || ch == '1') {
            bits[len++] = (char)ch;
        } else if (!isspace(ch) && ch != '_') {
            fprintf(stderr, "invalid BLC character: %c\n", ch);
            exit(1);
        }
    }
    bits[len] = 0;
    if (!len) {
        die("empty BLC input");
    }
    return bits;
}

static char *unpack_blc(const unsigned char *raw, long n) {
    char *bits;
    long i;
    int bit;
    long len;

    bits = (char *)xmalloc((size_t)(n * 8 + 1));
    len = 0;
    for (i = 0; i < n; i++) {
        for (bit = 7; bit >= 0; bit--) {
            bits[len++] = ((raw[i] >> bit) & 1) ? '1' : '0';
        }
    }
    bits[len] = 0;
    if (!len) {
        die("empty packed BLC input");
    }
    return bits;
}

static int has_suffix(const char *s, const char *suffix) {
    size_t slen;
    size_t tlen;

    slen = strlen(s);
    tlen = strlen(suffix);
    if (slen < tlen) {
        return 0;
    }
    return strcmp(s + slen - tlen, suffix) == 0;
}

static char *load_bits_arg(const char *path_or_bits, int inline_bits, int packed) {
    FILE *f;
    char *raw;
    char *bits;
    unsigned char *blob;
    long blob_len;

    if (inline_bits) {
        raw = (char *)xmalloc(strlen(path_or_bits) + 1);
        strcpy(raw, path_or_bits);
    } else if (strcmp(path_or_bits, "-") == 0) {
        blob = read_blob(stdin, &blob_len);
        if (packed) {
            bits = unpack_blc(blob, blob_len);
            free(blob);
            return bits;
        }
        raw = (char *)xmalloc((size_t)blob_len + 1);
        if (blob_len > 0) {
            memcpy(raw, blob, (size_t)blob_len);
        }
        raw[blob_len] = 0;
        free(blob);
    } else {
        f = fopen(path_or_bits, "rb");
        if (!f) {
            fprintf(stderr, "could not open input file: %s\n", path_or_bits);
            exit(1);
        }
        blob = read_blob(f, &blob_len);
        fclose(f);
        if (packed || has_suffix(path_or_bits, ".Blc")) {
            bits = unpack_blc(blob, blob_len);
            free(blob);
            return bits;
        }
        raw = (char *)xmalloc((size_t)blob_len + 1);
        if (blob_len > 0) {
            memcpy(raw, blob, (size_t)blob_len);
        }
        raw[blob_len] = 0;
        free(blob);
    }

    bits = clean_bits(raw);
    free(raw);
    return bits;
}

static void set_runtime_input(const unsigned char *data, long len) {
    if (len > IO_CAP) {
        die("runtime input too large");
    }
    if (len > 0) {
        memcpy(INPUT, data, (size_t)len);
    }
    INPUT_LEN = len;
    INPUT_POS = 0;
}

static void load_runtime_input_file(const char *path) {
    FILE *f;
    unsigned char *raw;
    long len;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "could not open input file: %s\n", path);
        exit(1);
    }
    raw = read_blob(f, &len);
    fclose(f);
    set_runtime_input(raw, len);
    free(raw);
}

static void load_runtime_stdin_if_piped(void) {
    unsigned char *raw;
    long len;

    if (isatty(0)) {
        return;
    }
    raw = read_blob(stdin, &len);
    set_runtime_input(raw, len);
    free(raw);
}

static void print_term_at(FILE *out, long term) {
    long kind = L[term];
    long i;

    if (term == U) {
        fprintf(out, "<input-thunk@END>");
        return;
    }
    if (kind == VAR) {
        fprintf(out, "%ld", L[term + 1]);
    } else if (kind == ABS) {
        fprintf(out, "\\.");
        print_term_at(out, term + 1);
    } else if (kind == APP) {
        fprintf(out, "(");
        print_term_at(out, term + 2);
        fprintf(out, " ");
        print_term_at(out, term + 2 + L[term + 1]);
        fprintf(out, ")");
    } else if (kind == IOP) {
        fprintf(out, "iop%ld", term);
    } else {
        fprintf(out, "<bad:%ld:%ld", term, kind);
        for (i = 1; i < 3; i++) {
            fprintf(out, " %ld", L[term + i]);
        }
        fprintf(out, ">");
    }
}

static long load_byte_list(int ch) {
    int i;
    long r;

    r = U;
    for (i = 7; i >= 0; --i) {
        append_code(ABS);
        append_code(APP);
        append_code(i ? 4 : backref(NIL));
        append_code(APP);
        append_code(backref(ch & (1 << i) ? FALSE : TRUE));
        append_code(VAR);
        append_code(0);
    }
    return r;
}

static void load_rom(void) {
    long i;
    long rom_len;

    rom_len = (long)(sizeof(kRom) / sizeof(kRom[0]));
    if (kRom[WR0] != IOP || kRom[WR1] != IOP || kRom[NIL] != ABS ||
        kRom[TRUE] != ABS) {
        die("kRom offset constants are out of sync");
    }
    for (i = 0; i < rom_len; i++) {
        append_code(kRom[i]);
    }
    L[4] = m ? 2 : 9;
    if (m) {
        for (i = 0; i < 256; i++) {
            kLazy[i] = load_byte_list((int)i);
        }
    }
    L[1] = U - 2;
}

static int read_input_byte(void) {
    unsigned char byte;

    if (INPUT_POS >= INPUT_LEN) {
        return -1;
    }
    byte = INPUT[INPUT_POS];
    rlog_push(TAG_READ_INPUT, (long)byte, INPUT_POS);
    INPUT_POS++;
    write_I((long)byte);
    return (int)byte;
}

static void output_byte(unsigned char byte, int tag) {
    if (OUT_LEN >= IO_CAP) {
        die("output buffer exhausted");
    }
    rlog_push(tag, (long)byte, OUT_LEN);
    OUT[OUT_LEN++] = byte;
    write_O((long)byte);
}

static void flush_output_to_stdout(void) {
    long written;
    long off;

    off = 0;
    while (off < OUT_LEN) {
        written = (long)write(1, OUT + off, (size_t)(OUT_LEN - off));
        if (written <= 0) {
            die("stdout write failed");
        }
        off += written;
    }
}

static void log_exit(int code) {
    rlog_push(TAG_EXIT, (long)code, (long)exit_code);
    halted = 1;
    exit_code = code;
}

static void step_app(void) {
    long term = C;
    long arg_term;
    long arg_env;
    long cell;

    cell = alloc_cell();
    arg_term = term + 2 + L[term + 1];
    arg_env = arg_term > KROM_LAST_IOP && arg_term != U ? a : 0;

    /*
     * IOCCC APP analogue:
     *   D=B?B:Calloc(4,X); B=*D; *D=c; c=D; D[2]=a;
     *   a[++D[1]]++; D[3]=++C+u
     */
    heap_write(CELL_NEXT(cell), c);
    heap_write(CELL_REFS(cell), 1);
    heap_write(CELL_ENV(cell), arg_env);
    ref_inc(arg_env);
    heap_write(CELL_TERM(cell), arg_term);
    write_c(cell);
    write_C(term + 2);

    if (trace) {
        fprintf(stderr, "APP  C=%ld a=%ld c=%ld residual=%ld\n",
                C, a, c, RLOG_LEN);
    }
}

static void step_abs(void) {
    long cell;

    if (!c) {
        log_exit(0);
        return;
    }

    cell = c;

    /*
     * IOCCC ABS analogue:
     *   D=c; c=*D; *D=a; a=D
     */
    write_D(cell);
    write_c(L[CELL_NEXT(cell)]);
    heap_write(CELL_NEXT(cell), a);
    write_a(cell);
    write_C(C + 1);

    if (trace) {
        fprintf(stderr, "ABS  C=%ld a=%ld c=%ld residual=%ld\n",
                C, a, c, RLOG_LEN);
    }
}

static void step_var(void) {
    long old_env;
    long target;
    long index;

    /*
     * IOCCC VAR analogue:
     *   s(D=a), C=a[3], ++1[a=a[2]], d(D)
     */
    old_env = a;
    index = L[C + 1];
    write_D(a);
    write_u(index);
    while (u > 0 && D) {
        write_D(L[CELL_NEXT(D)]);
        write_u(u - 1);
    }
    if (!D) {
        log_exit(2);
        return;
    }

    target = D;
    ref_inc(L[CELL_ENV(target)]);
    write_C(L[CELL_TERM(target)]);
    write_a(L[CELL_ENV(target)]);
    ref_dec_deferred(old_env);

    if (trace) {
        fprintf(stderr, "VAR%ld C=%ld a=%ld c=%ld residual=%ld\n",
                index, C, a, c, RLOG_LEN);
    }
}

static void gro_expand_nil(long old_end) {
    rlog_push(TAG_GRO_EXPAND, 4, old_end);
    append_runtime(ABS);
    append_runtime(ABS);
    append_runtime(VAR);
    append_runtime(0);
}

static void gro_expand_cons(long old_end, long value_term) {
    rlog_push(TAG_GRO_EXPAND, 7, old_end);
    append_runtime(ABS);
    append_runtime(APP);
    append_runtime(4);
    append_runtime(APP);
    append_runtime(value_term - (U + 1));
    append_runtime(VAR);
    append_runtime(0);
}

static void step_gro(void) {
    long old_end;
    long value_term;
    int byte;

    old_end = U;
    byte = read_input_byte();
    if (byte < 0) {
        gro_expand_nil(old_end);
        return;
    }

    if (m) {
        value_term = kLazy[(unsigned char)byte];
    } else {
        value_term = byte & 1 ? FALSE : TRUE;
    }
    gro_expand_cons(old_end, value_term);
}

static void step_put(void) {
    long bit;
    long old_co;

    if (!m) {
        output_byte((unsigned char)('0' + (C & 1)), TAG_PUT_BYTE);
        write_C(2);
        return;
    }

    if (L[C + 1] & 1) {
        output_byte((unsigned char)co, TAG_PUT_BYTE);
        write_C(2);
        return;
    }

    bit = C & 1;
    old_co = co;
    rlog_push(TAG_PUT_BIT, C, old_co);
    write_co((co << 1) | bit);
    write_C(9);
}

static void step_iop(void) {
    long old_env;

    old_env = a;
    if (C == U) {
        step_gro();
    } else {
        step_put();
    }
    ref_dec_deferred(old_env);
    write_a(0);
}

static int step_forward(void) {
    long kind;

    if (halted) {
        return 0;
    }
    kind = L[C];
    if (kind == APP) {
        step_app();
        return 1;
    }
    if (kind == ABS) {
        step_abs();
        return !halted;
    }
    if (kind == VAR) {
        step_var();
        return !halted;
    }
    if (kind == IOP) {
        step_iop();
        return !halted;
    }
    log_exit(3);
    return 0;
}

static void restore_scalar(int tag, long old) {
    switch (tag) {
    case TAG_A:
        a = old;
        break;
    case TAG_C:
        C = old;
        break;
    case TAG_D:
        D = old;
        break;
    case TAG_U:
        U = old;
        break;
    case TAG_B:
        B = old;
        break;
    case TAG_c:
        c = old;
        break;
    case TAG_b:
        b = old;
        break;
    case TAG_m:
        m = old;
        break;
    case TAG_u:
        u = old;
        break;
    case TAG_H:
        H = old;
        break;
    case TAG_I:
        I = old;
        break;
    case TAG_O:
        O = old;
        break;
    case TAG_co:
        co = old;
        break;
    case TAG_END:
        U = old;
        break;
    default:
        fprintf(stderr, "not a scalar tag: %s\n", rlog_tag_name(tag));
        exit(1);
    }
}

static void run_backward(void) {
    long i;
    RLogEntry e;

    for (i = RLOG_LEN - 1; i >= 0; i--) {
        e = RLOG[i];
        switch (e.tag) {
        case TAG_HEAP:
            L[e.addr] = e.old;
            break;
        case TAG_A:
        case TAG_C:
        case TAG_D:
        case TAG_U:
        case TAG_B:
        case TAG_c:
        case TAG_b:
        case TAG_m:
        case TAG_u:
        case TAG_H:
        case TAG_I:
        case TAG_O:
        case TAG_co:
        case TAG_END:
            restore_scalar(e.tag, e.old);
            break;
        case TAG_REFCNT_DEC:
            L[CELL_REFS(e.addr)] = e.old;
            break;
        case TAG_FREELIST_PUSH:
            L[CELL_NEXT(e.addr)] = e.old;
            break;
        case TAG_ALLOC_POP:
        case TAG_ALLOC_FRESH:
            break;
        case TAG_READ_INPUT:
            INPUT_POS = e.old;
            break;
        case TAG_GRO_EXPAND:
            if (e.old < 0 || e.old + e.addr > ARENA_WORDS) {
                die("bad gro residual");
            }
            memset(L + e.old, 0, (size_t)e.addr * sizeof(L[0]));
            U = e.old;
            break;
        case TAG_PUT_BIT:
            co = e.old;
            break;
        case TAG_PUT_BYTE:
            OUT_LEN = e.old;
            break;
        case TAG_EXIT:
            halted = 0;
            exit_code = (int)e.old;
            break;
        default:
            fprintf(stderr, "unknown residual tag: %d\n", e.tag);
            exit(1);
        }
    }
}

static void snapshot_state(void) {
    memcpy(L_SNAPSHOT, L, sizeof(L));
    snap_a = a;
    snap_C = C;
    snap_c = c;
    snap_D = D;
    snap_U = U;
    snap_B = B;
    snap_b = b;
    snap_m = m;
    snap_u = u;
    snap_H = H;
    snap_I = I;
    snap_O = O;
    snap_co = co;
    memcpy(kLazy_snapshot, kLazy, sizeof(kLazy));
}

static int heap_restored(void) {
    return memcmp(L, L_SNAPSHOT, sizeof(L)) == 0;
}

static int scalars_restored(void) {
    return a == snap_a && C == snap_C && c == snap_c && D == snap_D &&
           U == snap_U && B == snap_B && b == snap_b && m == snap_m &&
           u == snap_u && H == snap_H && I == snap_I && O == snap_O &&
           co == snap_co;
}

static int klazy_restored(void) {
    return memcmp(kLazy, kLazy_snapshot, sizeof(kLazy)) == 0;
}

static void print_usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--bits BITS|FILE|-] [--input TEXT|--input-file FILE] "
            "[--byte|--bit] [--packed] [--no-prelude] [--max-steps N] "
            "[--trace] [--dump-residual FILE]\n",
            argv0);
}

/*
 * Dumps RLOG to FILE as a sequence of fixed-width records:
 *   magic     "RLOG0001" (8 bytes)
 *   count     int64_le
 *   then count records of (tag:int32_le, addr:int64_le, old:int64_le).
 * Header writes are little-endian regardless of host so measure.py
 * can mmap/struct.unpack without worrying about portability.
 */
static int dump_residual_log(const char *path, long start, long end) {
    FILE *f = fopen(path, "wb");
    long i;
    int64_t count;
    unsigned char buf[20];
    if (!f) {
        fprintf(stderr, "dump_residual_log: cannot open %s\n", path);
        return 0;
    }
    if (fwrite("RLOG0001", 1, 8, f) != 8) goto fail;
    count = (int64_t)(end - start);
    for (i = 0; i < 8; i++) buf[i] = (unsigned char)((count >> (8 * i)) & 0xff);
    if (fwrite(buf, 1, 8, f) != 8) goto fail;
    for (i = start; i < end; i++) {
        int32_t t = (int32_t)RLOG[i].tag;
        int64_t a = (int64_t)RLOG[i].addr;
        int64_t o = (int64_t)RLOG[i].old;
        int j;
        for (j = 0; j < 4; j++) buf[j]      = (unsigned char)((t >> (8 * j)) & 0xff);
        for (j = 0; j < 8; j++) buf[4 + j]  = (unsigned char)((a >> (8 * j)) & 0xff);
        for (j = 0; j < 8; j++) buf[12 + j] = (unsigned char)((o >> (8 * j)) & 0xff);
        if (fwrite(buf, 1, 20, f) != 20) goto fail;
    }
    fclose(f);
    return 1;
fail:
    fclose(f);
    fprintf(stderr, "dump_residual_log: write failed\n");
    return 0;
}

int main(int argc, char **argv) {
    char *bits = 0;
    Parser parser;
    long root;
    long gotoget;
    long main_offset;
    long parse_start;
    long parse_len;
    long max_steps = DEFAULT_MAX_STEPS;
    long steps = 0;
    long before_log;
    int i;
    int inline_bits = 0;
    int packed = 0;
    int allow_trailing_bits;
    int runtime_input_set = 0;
    const char *source = 0;
    const char *dump_path = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bits") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            source = argv[++i];
            inline_bits = 1;
        } else if (strcmp(argv[i], "--max-steps") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            max_steps = strtol(argv[++i], 0, 10);
            if (max_steps <= 0) {
                die("invalid max step count");
            }
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--byte") == 0) {
            m = 1;
        } else if (strcmp(argv[i], "--bit") == 0) {
            m = 0;
        } else if (strcmp(argv[i], "--no-prelude") == 0) {
            use_prelude = 0;
        } else if (strcmp(argv[i], "--packed") == 0) {
            packed = 1;
        } else if (strcmp(argv[i], "--input") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            i++;
            set_runtime_input((const unsigned char *)argv[i],
                              (long)strlen(argv[i]));
            runtime_input_set = 1;
        } else if (strcmp(argv[i], "--input-file") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            load_runtime_input_file(argv[++i]);
            runtime_input_set = 1;
        } else if (strcmp(argv[i], "--dump-residual") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            dump_path = argv[++i];
        } else if (!source) {
            source = argv[i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!source) {
        source = "-";
    }

    allow_trailing_bits = packed || (!inline_bits && has_suffix(source, ".Blc"));
    bits = load_bits_arg(source, inline_bits, packed);
    if (!runtime_input_set && strcmp(source, "-") != 0) {
        load_runtime_stdin_if_piped();
    }

    U = 0;
    if (use_prelude) {
        load_rom();
        append_code(APP);
        gotoget = U;
        append_code(0);
        main_offset = U;
    } else {
        gotoget = -1;
        main_offset = U;
    }
    parser.bits = bits;
    parser.len = strlen(bits);
    parser.pos = 0;
    parse_start = parse_term(&parser);
    root = parse_start;
    if (!allow_trailing_bits && parser.pos != parser.len) {
        fprintf(stderr, "trailing BLC bits at offset %lu\n",
                (unsigned long)parser.pos);
        return 1;
    }
    parse_len = U - main_offset;
    if (use_prelude) {
        L[gotoget] = parse_len;
    }

    C = use_prelude ? 0 : root;
    a = 0;
    c = 0;
    D = 0;
    B = 0;
    b = 0;
    u = 0;
    I = 0;
    O = 0;
    co = 0;
    H = HEAP_BASE;

    snapshot_state();

    fprintf(stderr, "krivine_rev: reversible instrumentation of BLC/Krivine core\n");
    fprintf(stderr, "mode: %s, prelude: %s\n", m ? "byte" : "bit",
            use_prelude ? "on" : "off");
    fprintf(stderr, "program bits: %s\n", bits);
    fprintf(stderr, "runtime input bytes: %ld\n", INPUT_LEN);
    fprintf(stderr, "decoded root: ");
    print_term_at(stderr, root);
    fprintf(stderr, "\n");
    fprintf(stderr, "snapshot: post-parse, pre-reduce\n");

    before_log = RLOG_LEN;
    while (!halted && steps < max_steps) {
        step_forward();
        steps++;
    }
    if (!halted) {
        log_exit(124);
    }

    fprintf(stderr, "\nforward:\n");
    fprintf(stderr, "  steps: %ld\n", steps);
    fprintf(stderr, "  exit code: %d\n", exit_code);
    fprintf(stderr, "  residual entries: %ld\n", RLOG_LEN - before_log);
    if (dump_path) {
        if (!dump_residual_log(dump_path, before_log, RLOG_LEN)) return 1;
        fprintf(stderr, "  residual dump: %s\n", dump_path);
    }
    fprintf(stderr, "  current term: ");
    print_term_at(stderr, C);
    fprintf(stderr, "\n");
    fprintf(stderr, "  output bytes buffered: %ld\n", OUT_LEN);
    flush_output_to_stdout();

    run_backward();

    fprintf(stderr, "\nbackward:\n");
    fprintf(stderr, "  heap restored: %s\n", heap_restored() ? "yes" : "no");
    fprintf(stderr, "  scalars restored: %s\n", scalars_restored() ? "yes" : "no");
    fprintf(stderr, "  kLazy restored: %s\n", klazy_restored() ? "yes" : "no");
    fprintf(stderr, "  output buffer empty: %s\n", OUT_LEN == 0 ? "yes" : "no");
    fprintf(stderr, "  input logically unconsumed: %s\n", INPUT_POS == 0 ? "yes" : "no");
    fprintf(stderr, "  round trip: %s\n",
           heap_restored() && scalars_restored() && OUT_LEN == 0 &&
                   INPUT_POS == 0 && klazy_restored()
               ? "yes"
               : "no");

    free(bits);
    return heap_restored() && scalars_restored() && OUT_LEN == 0 &&
                   INPUT_POS == 0 && klazy_restored()
               ? 0
               : 1;
}
