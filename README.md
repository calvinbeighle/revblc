# revblc

Reversible execution experiments for Binary Lambda Calculus (BLC).

`revblc` explores a simple question:

```text
What information must a lambda-calculus evaluator preserve if we want to run it backward?
```

A normal evaluator computes:

```text
program -> result
```

That loses information. This project makes the missing information explicit and
checks that the computation can be reversed:

```text
program -> result + residual state
result + residual state -> program
```

The repo contains three runnable C artifacts:

```text
revblc.c                  readable trace-reversible BLC reducer
native_beta.c             native reversible beta-step experiment
instrumented_krivine/     fuller instrumented evaluator with I/O and packed input tests
```

## Quick start

```sh
make test
make test-reference
```

Run individual demos:

```sh
./revblc examples/identity_app.blc
./native_beta examples/native_duplication.blc
./instrumented_krivine/krivine_rev examples/io/cat.blc --input 0101
./instrumented_krivine/krivine_rev examples/io/reverse.Blc --byte --input abc
```

## 1. `revblc.c`: trace-reversible reduction

`revblc.c` is the readable baseline.

It evaluates raw BLC syntax with the usual Krivine-style transitions:

```text
APP  application
ABS  abstraction
VAR  variable lookup
```

The forward run records the residual information needed to undo each step. Then
it copies the result closure, runs the residuals backward, and checks that the
original machine state is restored.

Example:

```sh
./revblc examples/identity_app.blc
```

Input:

```text
0100100010
```

which encodes:

```text
((\.0) (\.0))
```

Forward trace:

```text
app  push argument closure; continue with function; save app marker
abs  pop argument into environment; continue with body; save abs marker
var  replace variable by environment closure; save index and old environment
```

Backward trace:

```text
var -> abs -> app
```

The important observation is that `APP` and `ABS` only need compact markers, but
`VAR` must preserve lookup context because lookup overwrites the current
closure.

## 2. `native_beta.c`: reversible beta without storing the whole redex

`native_beta.c` explores a more interesting direction: make one beta step
reversible as a calculus rule, not merely as a logged machine step.

Ordinary beta reduction is many-to-one:

```text
((\ body) arg) -> body[arg / 0]
```

The reversible version returns an extended visible state:

```text
((\ body) arg) <-> beta_witness(reduct, witness)
```

The witness depends on how the bound variable is used:

```text
erasure      store the erased argument
linear use   store the occurrence boundary
duplication  store the occurrence group and verify equal copies fold back
```

Run:

```sh
./native_beta examples/native_erasure.blc
./native_beta examples/identity_app.blc
./native_beta examples/native_duplication.blc
```

This is the most novel piece in the repo. The trace reducers establish a
baseline; `native_beta.c` tries to beat that baseline by storing the collision
witness instead of the whole input.

## 3. `instrumented_krivine/`: fuller evaluator instrumentation

`instrumented_krivine/` is the larger executable harness.

It records reducer mutations as residual state, runs the residual log backward,
and verifies restoration of:

```text
heap/code arena
machine scalars
lazy input state
output state
```

It also exercises runtime input/output and packed `.Blc` programs.

Run:

```sh
cd instrumented_krivine
make test
make test-reference
```

Example checks report:

```text
heap restored: yes
scalars restored: yes
kLazy restored: yes
output buffer empty: yes
input logically unconsumed: yes
round trip: yes
```

## Project layout

```text
revblc/
├── README.md
├── Makefile
├── revblc.c
├── native_beta.c
├── instrumented_krivine/
├── examples/
├── reference/
├── traces/
├── NATIVE_BETA.md
├── REVERSIBLE_KRIVINE.md
├── TROMP_PROTOCOL.md
├── OPEN_PROBLEMS.md
└── REVIEW_REQUEST.md
```

## Scope

This is not a complete native reversible lambda calculus.

It is a working set of experiments for identifying and testing the residual
information needed to reverse BLC evaluation steps. The trace-reversible
reducers are the baseline. The native beta rule is the first step toward making
individual calculus rules reversible over visible extended state.
