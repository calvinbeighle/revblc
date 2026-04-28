# revblc

`revblc` contains three small C artifacts:

- `tromp_reversible/`: the answer-track for Justine's literal Tromp prompt.
- `revblc.c`: a tiny trace-reversible BLC-syntax Krivine reducer.
- `native_beta.c`: one native reversible beta rule with collision witnesses.

It is meant to be the smallest useful artifact for the redbean `#lambda`
discussion: something you can compile, run, mutate, and argue with.

The project is anchored on the C Krivine machine jartine pasted from John
Tromp's IOCCC 2012 entry as the starting point for this project. It also keeps
the RosettaCode Phix Universal Lambda Machine around as a readable
cross-reference.

The relevant transition shapes are:

```text
VAR  variable lookup
APP  application
ABS  abstraction
```

The difference is that `revblc.c` records explicit Bennett-style residual state
for every transition, copies the weak-head result closure, then runs the
residuals backward to restore the original machine state.

This is intentionally honest about its scope:

- `revblc.c` is not a native reversible lambda calculus.
- It is not Tromp's full universal machine input/output protocol.
- It reduces raw lambda terms encoded in BLC syntax to weak-head normal form.

`native_beta.c` is the first native-rule experiment. It applies one top-level
beta step and returns an extended reversible state that carries only the
erasure, boundary, or duplication witness needed to invert that beta step.

## Build

```sh
make
```

or directly:

```sh
cc -std=c89 -Wall -Wextra -pedantic -O2 -o revblc revblc.c
cc -std=c89 -Wall -Wextra -pedantic -O2 -o native_beta native_beta.c
cd tromp_reversible && cc -std=c99 -Wall -Wextra -pedantic -O2 -o tromp_rev tromp.c
```

## Run

```sh
./revblc
./revblc examples/identity_app.blc
./revblc --bits 0100100010
./native_beta examples/identity_app.blc
./tromp_reversible/tromp_rev tromp_reversible/examples/identity_app.blc
./tromp_reversible/tromp_rev tromp_reversible/examples/io/cat.blc --input 0101
make native
make traces
```

Default input:

```text
0100100010
```

which is:

```text
((\.0) (\.0))
```

## What It Shows

A normal Krivine reducer computes forward:

```text
program -> result
```

`revblc` demonstrates Bennett-style compute-copy-uncompute:

```text
program -> result closure + residuals
copy result closure
run residuals backward
original program state + copied result closure
```

The important point is not speed. The important point is information
accounting. If a step would normally throw away context, `revblc` makes the
context visible as residual state.

For this tiny program, the forward trace is:

```text
app  push argument closure; continue with function; save app marker
abs  pop argument into environment; continue with body; save abs marker
var  replace variable by environment closure; save index and old environment
```

Then the reducer consumes those residuals backward and reconstructs the
original application while retaining the copied result closure.

## Why This Exists

Justine Tunney suggested making Tromp's lambda calculus interpreter reversible:
take the primitives of a Krivine machine and save enough information to undo
them.

This is not a full replacement for Tromp's IOCCC interpreter and it is not the
native no-log reversible calculus Lewpen was gesturing at. It is a small,
portable C workbench for making the trace-reversal baseline concrete before
trying to beat it.

The exact pasted Tromp source is included as:

```text
reference/tromp-krivine-ioccc2012.c
```

The Phix implementation from RosettaCode is included as a secondary readable
reference in:

```text
reference/rosettacode-phix.txt
```

Original RosettaCode page: `https://rosettacode.org/wiki/Universal_Lambda_Machine#Phix`

The `tromp_reversible/` directory is the place to look first if the question is
"did you actually modify Tromp's interpreter path?" It un-golfs the
Tromp/Krivine transition structure, records every reducer mutation in a
residual log, runs backward, and verifies the heap/scalars against a pre-reduce
snapshot. It now includes the Tromp/Justine runtime wrapper, lazy input-list
expansion, `wr0`/`wr1`, bit-mode output, byte-mode output, and the `kLazy[256]`
table. Packed `.Blc` program parsing is supported, and `make test-reference`
compares packed cat and `reverse.Blc` byte-for-byte against Tromp's `uni`.

There is also a short pasteable note in `docs/discord-post.md`.

The native beta rule is documented in `docs/native-beta.md`.

The Tromp/Justine runtime protocol is documented in `docs/tromp-protocol.md`.
