/*
 * revblc.c - a tiny trace-reversible BLC-syntax Krivine reducer.
 *
 * Reference points:
 *   1. John Tromp's IOCCC 2012 BLC Krivine machine, pasted in redbean
 *      #lambda by jartine as the concrete starting point for this project.
 *   2. Rosetta Code "Universal Lambda Machine", Phix implementation, used
 *      as a readable spelling of the same VAR / APP / ABS transition shapes.
 *
 * This file keeps those Krivine transition shapes, but records explicit
 * Bennett-style residual state so each step can be run backward. It is not
 * yet a native reversible lambda calculus and it is not Tromp's full ULM I/O
 * protocol.
 *
 * Build:
 *   cc -std=c89 -Wall -Wextra -pedantic -O2 -o revblc revblc.c
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TERM_VAR 1
#define TERM_APP 2
#define TERM_ABS 3

#define RES_APP 1
#define RES_ABS 2
#define RES_VAR 3

#define DEFAULT_PROGRAM "0100100010"
#define DEFAULT_MAX_STEPS 1000

typedef struct Term Term;
typedef struct Env Env;
typedef struct Frame Frame;
typedef struct Residual Residual;
typedef struct Allocation Allocation;

typedef struct Closure {
    Term *term;
    Env *env;
} Closure;

struct Term {
    int kind;
    int var;
    Term *left;
    Term *right;
};

struct Env {
    Closure closure;
    Env *next;
};

struct Frame {
    Closure closure;
    Frame *next;
};

struct Residual {
    int kind;
    int index;
    Env *env;
    Residual *next;
};

typedef struct Machine {
    Closure control;
    Frame *stack;
    Residual *residual;
    int residual_payload_bits;
} Machine;

typedef struct Parser {
    char *bits;
    size_t len;
    size_t pos;
} Parser;

struct Allocation {
    void *ptr;
    Allocation *next;
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

static Closure closure_new(Term *term, Env *env) {
    Closure c;
    c.term = term;
    c.env = env;
    return c;
}

static Env *env_cons(Closure closure, Env *next) {
    Env *e;
    e = (Env *)xmalloc(sizeof(*e));
    e->closure = closure;
    e->next = next;
    return e;
}

static Closure *env_get(Env *env, int index) {
    int i;
    i = 0;
    while (env && i < index) {
        env = env->next;
        i++;
    }
    return env ? &env->closure : 0;
}

static void stack_push(Frame **stack, Closure closure) {
    Frame *f;
    f = (Frame *)xmalloc(sizeof(*f));
    f->closure = closure;
    f->next = *stack;
    *stack = f;
}

static int stack_pop(Frame **stack, Closure *out) {
    Frame *f;
    if (!*stack) {
        return 0;
    }
    f = *stack;
    *out = f->closure;
    *stack = f->next;
    return 1;
}

static int env_bits(Env *env);

static int residual_item_bits(int kind, int index, Env *env) {
    if (kind == RES_VAR) {
        return index + 2 + env_bits(env);
    }
    return 1;
}

static void residual_push(Machine *m, int kind, int index, Env *env) {
    Residual *r;
    r = (Residual *)xmalloc(sizeof(*r));
    r->kind = kind;
    r->index = index;
    r->env = env;
    r->next = m->residual;
    m->residual = r;
    m->residual_payload_bits += residual_item_bits(kind, index, env);
}

static int residual_pop(Machine *m, Residual *out) {
    Residual *r;
    if (!m->residual) {
        return 0;
    }
    r = m->residual;
    *out = *r;
    m->residual = r->next;
    m->residual_payload_bits -= residual_item_bits(r->kind, r->index, r->env);
    return 1;
}

static int term_equal(Term *a, Term *b);
static int env_equal(Env *a, Env *b);

static int closure_equal(Closure a, Closure b) {
    return term_equal(a.term, b.term) && env_equal(a.env, b.env);
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

static int env_equal(Env *a, Env *b) {
    while (a && b) {
        if (!closure_equal(a->closure, b->closure)) {
            return 0;
        }
        a = a->next;
        b = b->next;
    }
    return !a && !b;
}

static int stack_equal(Frame *a, Frame *b) {
    while (a && b) {
        if (!closure_equal(a->closure, b->closure)) {
            return 0;
        }
        a = a->next;
        b = b->next;
    }
    return !a && !b;
}

static int machine_equal(Machine *a, Machine *b) {
    return closure_equal(a->control, b->control) &&
           stack_equal(a->stack, b->stack) &&
           !a->residual && !b->residual;
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

static char *load_source(int argc, char **argv, int *max_steps_arg) {
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
        *max_steps_arg = 2;
    } else if (strcmp(argv[1], "--bits") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s [FILE|-|--bits BITS] [MAX_STEPS]\n", argv[0]);
            exit(1);
        }
        raw = (char *)xmalloc(strlen(argv[2]) + 1);
        strcpy(raw, argv[2]);
        *max_steps_arg = 3;
    } else if (strcmp(argv[1], "-") == 0) {
        raw = read_stream(stdin);
        *max_steps_arg = 2;
    } else {
        f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "could not open input file: %s\n", argv[1]);
            exit(1);
        }
        raw = read_stream(f);
        fclose(f);
        *max_steps_arg = 2;
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

static int env_depth(Env *env) {
    int n;
    n = 0;
    while (env) {
        n++;
        env = env->next;
    }
    return n;
}

static void print_closure(FILE *out, Closure c) {
    print_term(out, c.term);
    fprintf(out, " {env_depth=%d}", env_depth(c.env));
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

static int env_bits(Env *env);

static int closure_bits(Closure c) {
    return term_bits(c.term) + env_bits(c.env);
}

static int env_bits(Env *env) {
    int bits;
    bits = 1;
    while (env) {
        bits += 1 + closure_bits(env->closure);
        env = env->next;
    }
    return bits;
}

static int stack_bits(Frame *stack) {
    int bits;
    bits = 0;
    while (stack) {
        bits += closure_bits(stack->closure);
        stack = stack->next;
    }
    return bits;
}

static int residual_depth(Residual *r) {
    int n;
    n = 0;
    while (r) {
        n++;
        r = r->next;
    }
    return n;
}

static int machine_bits(Machine *m) {
    return closure_bits(m->control) + stack_bits(m->stack) +
           m->residual_payload_bits;
}

static int stack_depth(Frame *stack) {
    int n;
    n = 0;
    while (stack) {
        n++;
        stack = stack->next;
    }
    return n;
}

static const char *step_name(int kind) {
    if (kind == RES_APP) {
        return "app";
    }
    if (kind == RES_ABS) {
        return "abs";
    }
    if (kind == RES_VAR) {
        return "var";
    }
    return "?";
}

static const char *step_note(int kind) {
    if (kind == RES_APP) {
        return "push argument closure; continue with function; save app marker";
    }
    if (kind == RES_ABS) {
        return "pop argument into environment; continue with body; save abs marker";
    }
    if (kind == RES_VAR) {
        return "replace variable by environment closure; save index and old environment";
    }
    return "";
}

static int is_halted_whnf(Machine *m) {
    return m->control.term->kind == TERM_ABS && !m->stack;
}

static int is_stuck(Machine *m) {
    Term *t;
    t = m->control.term;
    if (t->kind == TERM_VAR && !env_get(m->control.env, t->var)) {
        return 1;
    }
    return 0;
}

static int step_forward(Machine *m) {
    Term *t;
    Closure arg;
    Closure *target;
    Env *old_env;

    t = m->control.term;
    if (t->kind == TERM_APP) {
        arg = closure_new(t->right, m->control.env);
        stack_push(&m->stack, arg);
        m->control.term = t->left;
        residual_push(m, RES_APP, 0, 0);
        return RES_APP;
    }
    if (t->kind == TERM_ABS && m->stack) {
        if (!stack_pop(&m->stack, &arg)) {
            return 0;
        }
        m->control.env = env_cons(arg, m->control.env);
        m->control.term = t->left;
        residual_push(m, RES_ABS, 0, 0);
        return RES_ABS;
    }
    if (t->kind == TERM_VAR) {
        target = env_get(m->control.env, t->var);
        if (!target) {
            return 0;
        }
        old_env = m->control.env;
        residual_push(m, RES_VAR, t->var, old_env);
        m->control = *target;
        return RES_VAR;
    }
    return 0;
}

static int step_backward(Machine *m) {
    Residual r;
    Closure arg;
    Env *head;

    if (!residual_pop(m, &r)) {
        return 0;
    }

    if (r.kind == RES_APP) {
        if (!stack_pop(&m->stack, &arg)) {
            fprintf(stderr, "reverse app failed: empty argument stack\n");
            exit(1);
        }
        if (!env_equal(arg.env, m->control.env)) {
            fprintf(stderr, "reverse app failed: argument and function env differ\n");
            exit(1);
        }
        m->control.term = term_app(m->control.term, arg.term);
        return RES_APP;
    }

    if (r.kind == RES_ABS) {
        head = m->control.env;
        if (!head) {
            fprintf(stderr, "reverse abs failed: empty environment\n");
            exit(1);
        }
        arg = head->closure;
        m->control.env = head->next;
        m->control.term = term_abs(m->control.term);
        stack_push(&m->stack, arg);
        return RES_ABS;
    }

    if (r.kind == RES_VAR) {
        m->control.term = term_var(r.index);
        m->control.env = r.env;
        return RES_VAR;
    }

    fprintf(stderr, "corrupt residual\n");
    exit(1);
}

static void print_state_line(Machine *m) {
    printf("  control: ");
    print_closure(stdout, m->control);
    printf(" | stack=%d | residual_items=%d | residual_bits=%d | payload_bits=%d\n",
           stack_depth(m->stack), residual_depth(m->residual),
           m->residual_payload_bits, machine_bits(m));
}

static int parse_max_steps(int argc, char **argv, int max_steps_arg) {
    long n;
    if (argc <= max_steps_arg) {
        return DEFAULT_MAX_STEPS;
    }
    n = strtol(argv[max_steps_arg], 0, 10);
    if (n <= 0 || n > 1000000L) {
        fprintf(stderr, "invalid max step count: %s\n", argv[max_steps_arg]);
        exit(1);
    }
    return (int)n;
}

int main(int argc, char **argv) {
    Parser p;
    Term *root;
    Machine machine;
    Machine initial;
    Closure copied_result;
    char *bits;
    int max_steps;
    int max_steps_arg;
    int steps;
    int kind;
    int before_bits;
    int after_bits;
    int before_residual;
    int after_residual;
    int reversed;
    int restored;
    int halted;
    int stuck;
    int copied;
    int rc;

    copied_result = closure_new(0, 0);
    copied = 0;
    bits = load_source(argc, argv, &max_steps_arg);
    max_steps = parse_max_steps(argc, argv, max_steps_arg);
    p.bits = bits;
    p.len = strlen(bits);
    p.pos = 0;
    root = parse_term(&p);
    if (p.pos != p.len) {
        fprintf(stderr, "trailing BLC bits at offset %lu\n", (unsigned long)p.pos);
        return 1;
    }

    machine.control = closure_new(root, 0);
    machine.stack = 0;
    machine.residual = 0;
    machine.residual_payload_bits = 0;
    initial = machine;

    printf("revblc: trace-reversible BLC-syntax Krivine reducer\n");
    printf("mode: Bennett-style residual trace + compute-copy-uncompute\n");
    printf("source: Tromp IOCCC 2012 Krivine core; RosettaCode Phix as readable cross-reference\n");
    printf("scope: raw BLC term reducer, not full Tromp ULM input/output\n");
    printf("input bits: %s\n", bits);
    printf("decoded: ");
    print_term(stdout, root);
    printf("\n\ninitial:\n");
    print_state_line(&machine);
    printf("\nforward:\n");

    steps = 0;
    while (steps < max_steps) {
        before_bits = machine_bits(&machine);
        before_residual = machine.residual_payload_bits;
        kind = step_forward(&machine);
        if (!kind) {
            break;
        }
        steps++;
        after_bits = machine_bits(&machine);
        after_residual = machine.residual_payload_bits;
        printf("%3d %-3s payload %d -> %d, residual %d -> %d\n",
               steps, step_name(kind), before_bits, after_bits,
               before_residual, after_residual);
        printf("      %s\n", step_note(kind));
    }
    if (steps == max_steps) {
        fprintf(stderr, "did not halt within %d steps\n", max_steps);
        return 1;
    }

    halted = is_halted_whnf(&machine);
    stuck = is_stuck(&machine);

    printf("\nstop status:\n");
    printf("  weak-head normal form: %s\n", halted ? "yes" : "no");
    printf("  stuck free variable: %s\n", stuck ? "yes" : "no");

    if (halted) {
        copied_result = machine.control;
        copied = 1;
        printf("\nresult closure copied to clean output before uncompute:\n");
        printf("  closure: ");
        print_closure(stdout, copied_result);
        printf("\n  term projection: ");
        print_term(stdout, copied_result.term);
        printf("\n  blc projection:  ");
        print_blc(stdout, copied_result.term);
        printf("\n");
    } else {
        printf("\nno result copied: reducer stopped before weak-head normal form\n");
    }
    print_state_line(&machine);

    printf("\nuncompute trace:\n");
    reversed = 0;
    while (machine.residual) {
        kind = machine.residual->kind;
        step_backward(&machine);
        reversed++;
        printf("%3d %-3s restored one %s transition\n",
               steps - reversed + 1, step_name(kind), step_name(kind));
    }

    restored = machine_equal(&machine, &initial);
    printf("\nrestored original state after uncompute: %s\n", restored ? "yes" : "no");
    printf("forward steps: %d\n", steps);
    printf("backward steps: %d\n", reversed);
    printf("final control after uncompute: ");
    print_closure(stdout, machine.control);
    printf("\n");
    if (copied) {
        printf("copied output retained: ");
        print_closure(stdout, copied_result);
        printf("\n");
    }

    rc = (halted && restored) ? 0 : 1;
    free_all_allocations();
    return rc;
}
