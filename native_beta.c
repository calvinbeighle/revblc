/*
 * native_beta.c - one native reversible beta rule for BLC terms.
 *
 * This is not a Krivine trace logger. It demonstrates a single reversible
 * beta transition over an extended visible state:
 *
 *   ((\ body) arg)  <->  witness(reduct, erased-arg-or-occurrence-paths)
 *
 * The witness is not the whole redex. It accounts for the exact collision:
 * erasure stores the erased argument; linear use stores only the occurrence
 * boundary needed by raw unlabelled syntax; duplication stores the occurrence
 * group so multiple copies can be folded back into one binder.
 *
 * Build:
 *   cc -std=c89 -Wall -Wextra -pedantic -O2 -o native_beta native_beta.c
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TERM_VAR 1
#define TERM_APP 2
#define TERM_ABS 3

#define CASE_ERASE 1
#define CASE_LINEAR 2
#define CASE_DUPLICATE 3

#define DEFAULT_PROGRAM "0100100010"

typedef struct Term Term;
typedef struct Parser Parser;
typedef struct Allocation Allocation;
typedef struct Path Path;
typedef struct PathList PathList;
typedef struct PathScratch PathScratch;
typedef struct NativeState NativeState;

struct Term {
    int kind;
    int var;
    Term *left;
    Term *right;
};

struct Parser {
    char *bits;
    size_t len;
    size_t pos;
};

struct Allocation {
    void *ptr;
    Allocation *next;
};

struct Path {
    char *steps;
    int len;
    int depth;
    Path *next;
};

struct PathList {
    Path *head;
    Path *tail;
    int count;
};

struct PathScratch {
    char *steps;
    int len;
    int cap;
};

struct NativeState {
    int kind;
    int occurrences;
    Term *reduct;
    Term *erased_arg;
    Path *paths;
};

static Allocation *allocations;

static void track_allocation(void *ptr) {
    Allocation *a;
    a = (Allocation *)malloc(sizeof(*a));
    if (!a) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    a->ptr = ptr;
    a->next = allocations;
    allocations = a;
}

static void track_reallocation(void *old_ptr, void *new_ptr) {
    Allocation *a;
    for (a = allocations; a; a = a->next) {
        if (a->ptr == old_ptr) {
            a->ptr = new_ptr;
            return;
        }
    }
    track_allocation(new_ptr);
}

static void free_all_allocations(void) {
    Allocation *a;
    Allocation *next;
    for (a = allocations; a; a = next) {
        next = a->next;
        free(a->ptr);
        free(a);
    }
    allocations = 0;
}

static void *xmalloc(size_t n) {
    void *p;
    p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    track_allocation(p);
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q;
    q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    track_reallocation(p, q);
    return q;
}

static Term *term_new(int kind, int var, Term *left, Term *right) {
    Term *t;
    t = (Term *)xmalloc(sizeof(*t));
    t->kind = kind;
    t->var = var;
    t->left = left;
    t->right = right;
    return t;
}

static Term *term_var(int index) {
    return term_new(TERM_VAR, index, 0, 0);
}

static Term *term_abs(Term *body) {
    return term_new(TERM_ABS, 0, body, 0);
}

static Term *term_app(Term *fun, Term *arg) {
    return term_new(TERM_APP, 0, fun, arg);
}

static Term *term_clone(Term *t) {
    if (t->kind == TERM_VAR) {
        return term_var(t->var);
    }
    if (t->kind == TERM_ABS) {
        return term_abs(term_clone(t->left));
    }
    return term_app(term_clone(t->left), term_clone(t->right));
}

static int term_equal(Term *a, Term *b) {
    if (a == b) {
        return 1;
    }
    if (!a || !b || a->kind != b->kind) {
        return 0;
    }
    if (a->kind == TERM_VAR) {
        return a->var == b->var;
    }
    if (a->kind == TERM_ABS) {
        return term_equal(a->left, b->left);
    }
    return term_equal(a->left, b->left) && term_equal(a->right, b->right);
}

static int term_closed_at(Term *t, int depth) {
    if (t->kind == TERM_VAR) {
        return t->var < depth;
    }
    if (t->kind == TERM_ABS) {
        return term_closed_at(t->left, depth + 1);
    }
    return term_closed_at(t->left, depth) && term_closed_at(t->right, depth);
}

static int bit_at(Parser *p) {
    if (p->pos >= p->len) {
        return -1;
    }
    return p->bits[p->pos++] == '1';
}

static Term *parse_term(Parser *p) {
    int bit;
    int bit2;
    int ones;
    Term *fun;
    Term *arg;
    Term *body;

    bit = bit_at(p);
    if (bit < 0) {
        fprintf(stderr, "unexpected end of BLC input\n");
        exit(1);
    }
    if (!bit) {
        bit2 = bit_at(p);
        if (bit2 < 0) {
            fprintf(stderr, "incomplete BLC tag\n");
            exit(1);
        }
        if (!bit2) {
            body = parse_term(p);
            return term_abs(body);
        }
        fun = parse_term(p);
        arg = parse_term(p);
        return term_app(fun, arg);
    }

    ones = 1;
    while ((bit = bit_at(p)) == 1) {
        ones++;
    }
    if (bit < 0) {
        fprintf(stderr, "unterminated BLC variable\n");
        exit(1);
    }
    return term_var(ones - 1);
}

static char *read_stream(FILE *f) {
    size_t cap;
    size_t len;
    int ch;
    char *buf;

    cap = 256;
    len = 0;
    buf = (char *)xmalloc(cap);
    while ((ch = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)xrealloc(buf, cap);
        }
        buf[len++] = (char)ch;
    }
    buf[len] = 0;
    return buf;
}

static char *load_source(int argc, char **argv) {
    FILE *f;
    char *raw;
    char *bits;
    size_t cap;
    size_t len;
    size_t i;
    int ch;

    if (argc < 2) {
        raw = (char *)xmalloc(strlen(DEFAULT_PROGRAM) + 1);
        strcpy(raw, DEFAULT_PROGRAM);
    } else if (strcmp(argv[1], "--bits") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s [FILE|-|--bits BITS]\n", argv[0]);
            exit(1);
        }
        raw = (char *)xmalloc(strlen(argv[2]) + 1);
        strcpy(raw, argv[2]);
    } else if (strcmp(argv[1], "-") == 0) {
        raw = read_stream(stdin);
    } else {
        f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "could not open input file: %s\n", argv[1]);
            exit(1);
        }
        raw = read_stream(f);
        fclose(f);
    }

    cap = strlen(raw) + 1;
    bits = (char *)xmalloc(cap);
    len = 0;
    for (i = 0; raw[i]; i++) {
        ch = (unsigned char)raw[i];
        if (ch == '0' || ch == '1') {
            bits[len++] = (char)ch;
        } else if (!isspace(ch) && ch != '_') {
            fprintf(stderr, "invalid BLC character: %c\n", ch);
            exit(1);
        }
    }
    bits[len] = 0;
    if (!len) {
        fprintf(stderr, "empty BLC input\n");
        exit(1);
    }
    return bits;
}

static void print_term(FILE *out, Term *t) {
    if (t->kind == TERM_VAR) {
        fprintf(out, "%d", t->var);
    } else if (t->kind == TERM_ABS) {
        fprintf(out, "\\.");
        print_term(out, t->left);
    } else {
        fprintf(out, "(");
        print_term(out, t->left);
        fprintf(out, " ");
        print_term(out, t->right);
        fprintf(out, ")");
    }
}

static void print_blc(FILE *out, Term *t) {
    int i;
    if (t->kind == TERM_VAR) {
        for (i = 0; i <= t->var; i++) {
            fputc('1', out);
        }
        fputc('0', out);
    } else if (t->kind == TERM_ABS) {
        fputs("00", out);
        print_blc(out, t->left);
    } else {
        fputs("01", out);
        print_blc(out, t->left);
        print_blc(out, t->right);
    }
}

static int term_bits(Term *t) {
    if (t->kind == TERM_VAR) {
        return t->var + 2;
    }
    if (t->kind == TERM_ABS) {
        return 2 + term_bits(t->left);
    }
    return 2 + term_bits(t->left) + term_bits(t->right);
}

static Term *term_shift_walk(Term *t, int cutoff, int delta) {
    int v;
    if (t->kind == TERM_VAR) {
        if (t->var >= cutoff) {
            v = t->var + delta;
            if (v < 0) {
                fprintf(stderr, "negative de Bruijn index during shift\n");
                exit(1);
            }
            return term_var(v);
        }
        return term_var(t->var);
    }
    if (t->kind == TERM_ABS) {
        return term_abs(term_shift_walk(t->left, cutoff + 1, delta));
    }
    return term_app(term_shift_walk(t->left, cutoff, delta),
                    term_shift_walk(t->right, cutoff, delta));
}

static Term *term_shift(Term *t, int delta) {
    return term_shift_walk(t, 0, delta);
}

static Term *term_subst_walk(Term *t, int index, int depth, Term *replacement) {
    if (t->kind == TERM_VAR) {
        if (t->var == index + depth) {
            return term_shift_walk(replacement, 0, depth);
        }
        return term_var(t->var);
    }
    if (t->kind == TERM_ABS) {
        return term_abs(term_subst_walk(t->left, index, depth + 1, replacement));
    }
    return term_app(term_subst_walk(t->left, index, depth, replacement),
                    term_subst_walk(t->right, index, depth, replacement));
}

static Term *beta_reduce(Term *body, Term *arg) {
    Term *shifted_arg;
    Term *substituted;
    shifted_arg = term_shift(arg, 1);
    substituted = term_subst_walk(body, 0, 0, shifted_arg);
    return term_shift(substituted, -1);
}

static void path_scratch_init(PathScratch *s) {
    s->cap = 32;
    s->len = 0;
    s->steps = (char *)xmalloc((size_t)s->cap);
}

static void path_scratch_push(PathScratch *s, char step) {
    if (s->len + 1 >= s->cap) {
        s->cap *= 2;
        s->steps = (char *)xrealloc(s->steps, (size_t)s->cap);
    }
    s->steps[s->len++] = step;
}

static void path_scratch_pop(PathScratch *s) {
    if (s->len <= 0) {
        fprintf(stderr, "internal path underflow\n");
        exit(1);
    }
    s->len--;
}

static void path_list_init(PathList *list) {
    list->head = 0;
    list->tail = 0;
    list->count = 0;
}

static void path_list_add(PathList *list, PathScratch *scratch, int depth) {
    Path *p;
    p = (Path *)xmalloc(sizeof(*p));
    p->steps = (char *)xmalloc((size_t)scratch->len + 1);
    if (scratch->len) {
        memcpy(p->steps, scratch->steps, (size_t)scratch->len);
    }
    p->steps[scratch->len] = 0;
    p->len = scratch->len;
    p->depth = depth;
    p->next = 0;
    if (list->tail) {
        list->tail->next = p;
    } else {
        list->head = p;
    }
    list->tail = p;
    list->count++;
}

static void collect_occurrences_walk(Term *t, int depth, PathScratch *scratch,
                                     PathList *out) {
    if (t->kind == TERM_VAR) {
        if (t->var == depth) {
            path_list_add(out, scratch, depth);
        }
        return;
    }
    if (t->kind == TERM_ABS) {
        path_scratch_push(scratch, 'b');
        collect_occurrences_walk(t->left, depth + 1, scratch, out);
        path_scratch_pop(scratch);
        return;
    }
    path_scratch_push(scratch, 'l');
    collect_occurrences_walk(t->left, depth, scratch, out);
    path_scratch_pop(scratch);
    path_scratch_push(scratch, 'r');
    collect_occurrences_walk(t->right, depth, scratch, out);
    path_scratch_pop(scratch);
}

static PathList collect_occurrences(Term *body) {
    PathScratch scratch;
    PathList out;
    path_list_init(&out);
    path_scratch_init(&scratch);
    collect_occurrences_walk(body, 0, &scratch, &out);
    return out;
}

static void print_path(FILE *out, Path *p) {
    int i;
    if (!p->len) {
        fputc('.', out);
        return;
    }
    for (i = 0; i < p->len; i++) {
        if (p->steps[i] == 'b') {
            fputc('B', out);
        } else if (p->steps[i] == 'l') {
            fputc('L', out);
        } else if (p->steps[i] == 'r') {
            fputc('R', out);
        } else {
            fputc('?', out);
        }
    }
}

static void print_paths(FILE *out, Path *paths) {
    Path *p;
    int first;
    first = 1;
    fputc('[', out);
    for (p = paths; p; p = p->next) {
        if (!first) {
            fputs(", ", out);
        }
        print_path(out, p);
        fprintf(out, "@depth%d", p->depth);
        first = 0;
    }
    fputc(']', out);
}

static int path_bits(Path *p) {
    return 1 + (2 * p->len);
}

static int path_list_bits(Path *paths) {
    int bits;
    Path *p;
    bits = 0;
    for (p = paths; p; p = p->next) {
        bits += path_bits(p);
    }
    return bits;
}

static void path_error(Path *p) {
    fprintf(stderr, "path does not address the reduct: ");
    print_path(stderr, p);
    fprintf(stderr, "\n");
    exit(1);
}

static Term *term_at_path(Term *t, Path *p) {
    int i;
    for (i = 0; i < p->len; i++) {
        if (p->steps[i] == 'b') {
            if (t->kind != TERM_ABS) {
                path_error(p);
            }
            t = t->left;
        } else if (p->steps[i] == 'l') {
            if (t->kind != TERM_APP) {
                path_error(p);
            }
            t = t->left;
        } else if (p->steps[i] == 'r') {
            if (t->kind != TERM_APP) {
                path_error(p);
            }
            t = t->right;
        } else {
            path_error(p);
        }
    }
    return t;
}

static Term *term_replace_at_path(Term *t, Path *p, int pos, Term *replacement) {
    if (pos == p->len) {
        return term_clone(replacement);
    }
    if (p->steps[pos] == 'b') {
        if (t->kind != TERM_ABS) {
            path_error(p);
        }
        return term_abs(term_replace_at_path(t->left, p, pos + 1, replacement));
    }
    if (p->steps[pos] == 'l') {
        if (t->kind != TERM_APP) {
            path_error(p);
        }
        return term_app(term_replace_at_path(t->left, p, pos + 1, replacement),
                        term_clone(t->right));
    }
    if (p->steps[pos] == 'r') {
        if (t->kind != TERM_APP) {
            path_error(p);
        }
        return term_app(term_clone(t->left),
                        term_replace_at_path(t->right, p, pos + 1, replacement));
    }
    path_error(p);
    return 0;
}

static Term *reconstruct_body_from_paths(Term *reduct, Path *paths) {
    Term *body;
    Term *marker;
    Path *p;
    body = term_clone(reduct);
    for (p = paths; p; p = p->next) {
        marker = term_var(p->depth);
        body = term_replace_at_path(body, p, 0, marker);
    }
    return body;
}

static const char *case_name(int kind) {
    if (kind == CASE_ERASE) {
        return "erasure";
    }
    if (kind == CASE_LINEAR) {
        return "linear";
    }
    if (kind == CASE_DUPLICATE) {
        return "duplication";
    }
    return "?";
}

static NativeState native_forward_beta(Term *root) {
    Term *body;
    Term *arg;
    PathList occs;
    NativeState state;

    if (root->kind != TERM_APP || root->left->kind != TERM_ABS) {
        fprintf(stderr, "top-level term is not a beta redex\n");
        exit(1);
    }
    if (!term_closed_at(root, 0)) {
        fprintf(stderr, "native_beta demo expects a closed top-level BLC term\n");
        exit(1);
    }

    body = root->left->left;
    arg = root->right;
    occs = collect_occurrences(body);

    state.occurrences = occs.count;
    state.reduct = beta_reduce(body, arg);
    state.erased_arg = 0;
    state.paths = occs.head;

    if (occs.count == 0) {
        state.kind = CASE_ERASE;
        state.erased_arg = term_clone(arg);
    } else if (occs.count == 1) {
        state.kind = CASE_LINEAR;
    } else {
        state.kind = CASE_DUPLICATE;
    }
    return state;
}

static Term *native_backward_beta(NativeState *state) {
    Term *body;
    Term *arg;
    Term *candidate;
    Term *check;
    Term *redex;
    Path *p;
    PathList check_occs;

    if (state->kind == CASE_ERASE) {
        body = term_shift(state->reduct, 1);
        check_occs = collect_occurrences(body);
        if (check_occs.count != 0) {
            fprintf(stderr, "erasure inverse reconstructed a body with occurrences\n");
            exit(1);
        }
        arg = term_clone(state->erased_arg);
    } else {
        if (!state->paths) {
            fprintf(stderr, "missing occurrence path witness\n");
            exit(1);
        }
        arg = term_clone(term_at_path(state->reduct, state->paths));
        for (p = state->paths; p; p = p->next) {
            candidate = term_at_path(state->reduct, p);
            if (!term_equal(candidate, arg)) {
                fprintf(stderr, "duplicate occurrence paths no longer hold equal terms\n");
                exit(1);
            }
        }
        body = reconstruct_body_from_paths(state->reduct, state->paths);
    }

    check = beta_reduce(body, arg);
    if (!term_equal(check, state->reduct)) {
        fprintf(stderr, "inverse failed beta verification\n");
        exit(1);
    }
    redex = term_app(term_abs(body), arg);
    return redex;
}

static void print_native_state(NativeState *state) {
    printf("%s_beta(", case_name(state->kind));
    printf("reduct=");
    print_term(stdout, state->reduct);
    if (state->kind == CASE_ERASE) {
        printf(", erased_arg=");
        print_term(stdout, state->erased_arg);
    } else {
        printf(", paths=");
        print_paths(stdout, state->paths);
    }
    printf(")");
}

static void print_witness_accounting(Term *root, NativeState *state) {
    int full_redex_bits;
    int erased_bits;
    int path_payload_bits;

    full_redex_bits = term_bits(root);
    erased_bits = state->erased_arg ? term_bits(state->erased_arg) : 0;
    path_payload_bits = path_list_bits(state->paths);

    printf("bound occurrences in beta body: %d\n", state->occurrences);
    printf("collision case: %s\n", case_name(state->kind));
    printf("naive whole-redex residual: %d BLC bits\n", full_redex_bits);
    printf("native witness:\n");
    if (state->kind == CASE_ERASE) {
        printf("  erased argument payload: %d BLC bits\n", erased_bits);
        printf("  occurrence paths: none\n");
    } else if (state->kind == CASE_LINEAR) {
        printf("  erased argument payload: 0 BLC bits\n");
        printf("  duplicate payload: 0 BLC bits\n");
        printf("  boundary path: ");
        print_paths(stdout, state->paths);
        printf(" (%d simple path bits)\n", path_payload_bits);
    } else {
        printf("  erased argument payload: 0 BLC bits\n");
        printf("  copied argument stored separately: no\n");
        printf("  duplicate group paths: ");
        print_paths(stdout, state->paths);
        printf(" (%d simple path bits)\n", path_payload_bits);
    }
}

int main(int argc, char **argv) {
    Parser parser;
    Term *root;
    Term *backward;
    NativeState state;
    char *bits;
    int ok;

    bits = load_source(argc, argv);
    parser.bits = bits;
    parser.len = strlen(bits);
    parser.pos = 0;
    root = parse_term(&parser);
    if (parser.pos != parser.len) {
        fprintf(stderr, "trailing BLC bits at offset %lu\n",
                (unsigned long)parser.pos);
        return 1;
    }

    state = native_forward_beta(root);
    backward = native_backward_beta(&state);
    ok = term_equal(root, backward);

    printf("native_beta: one native reversible beta rule for BLC terms\n");
    printf("scope: top-level beta redex, closed BLC term, one step\n");
    printf("input bits: %s\n", bits);
    printf("decoded redex: ");
    print_term(stdout, root);
    printf("\n\nforward native state:\n  ");
    print_native_state(&state);
    printf("\n  reduct blc: ");
    print_blc(stdout, state.reduct);
    printf("\n\n");
    print_witness_accounting(root, &state);

    printf("\nbackward reconstruction:\n  ");
    print_term(stdout, backward);
    printf("\n  blc: ");
    print_blc(stdout, backward);
    printf("\nround trip: %s\n", ok ? "yes" : "no");

    free_all_allocations();
    return ok ? 0 : 1;
}
