#ifndef TROMP_REVERSIBLE_RLOG_H
#define TROMP_REVERSIBLE_RLOG_H

typedef struct RLogEntry {
    int tag;
    long addr;
    long old;
} RLogEntry;

enum {
    TAG_HEAP = 1,
    TAG_A,
    TAG_C,
    TAG_D,
    TAG_U,
    TAG_B,
    TAG_c,
    TAG_b,
    TAG_m,
    TAG_u,
    TAG_H,
    TAG_I,
    TAG_O,
    TAG_co,
    TAG_END,
    TAG_REFCNT_DEC,
    TAG_FREELIST_PUSH,
    TAG_ALLOC_POP,
    TAG_ALLOC_FRESH,
    TAG_READ_INPUT,
    TAG_EXIT,
    TAG_GRO_EXPAND,
    TAG_PUT_BIT,
    TAG_PUT_BYTE
};

extern RLogEntry RLOG[];
extern long RLOG_LEN;

void rlog_push(int tag, long addr, long old);

#define LOG_HEAP(idx, oldv) rlog_push(TAG_HEAP, (long)(idx), (long)(oldv))
#define LOG_SCALAR(tag, oldv) rlog_push((tag), 0, (long)(oldv))

static const char *rlog_tag_name(int tag) {
    switch (tag) {
    case TAG_HEAP:
        return "heap";
    case TAG_A:
        return "a";
    case TAG_C:
        return "C";
    case TAG_D:
        return "D";
    case TAG_U:
        return "U";
    case TAG_B:
        return "B";
    case TAG_c:
        return "c";
    case TAG_b:
        return "b";
    case TAG_m:
        return "m";
    case TAG_u:
        return "u";
    case TAG_H:
        return "H";
    case TAG_I:
        return "I";
    case TAG_O:
        return "O";
    case TAG_co:
        return "co";
    case TAG_END:
        return "END";
    case TAG_REFCNT_DEC:
        return "refcnt_dec";
    case TAG_FREELIST_PUSH:
        return "freelist_push";
    case TAG_ALLOC_POP:
        return "alloc_pop";
    case TAG_ALLOC_FRESH:
        return "alloc_fresh";
    case TAG_READ_INPUT:
        return "read_input";
    case TAG_EXIT:
        return "exit";
    case TAG_GRO_EXPAND:
        return "gro_expand";
    case TAG_PUT_BIT:
        return "put_bit";
    case TAG_PUT_BYTE:
        return "put_byte";
    default:
        return "?";
    }
}

#endif
