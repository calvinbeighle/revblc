# instrumented_krivine

Reversible instrumentation for a BLC/Krivine evaluator.

The reference source is:

```text
../reference/tromp-krivine-ioccc2012.c
```

That file is the IOCCC 2012 Tromp source. It uses Cosmopolitan headers
and a heavily golfed comma-cascade. `tromp.c` is the
standalone, un-golfed C version of the same Krivine-core transition structure,
with reversible instrumentation around every reducer mutation.

Attribution:

```text
Original interpreter: John Tromp, IOCCC 2012, Public Domain
Reversibility modifications: Calvin Beighle
```

## Build

```sh
make
```

or:

```sh
cc -std=c99 -Wall -Wextra -pedantic -O2 -o krivine_rev tromp.c
```

## Run

```sh
./krivine_rev ../examples/identity_app.blc
printf '0100100010' | ./krivine_rev -
./krivine_rev ../examples/io/cat.blc --input 0101
./krivine_rev ../examples/io/cat.blc --byte --input A
./krivine_rev ../examples/io/reverse.Blc --byte --input abc
./krivine_rev ../examples/omega.blc --max-steps 12
make test
make test-reference
make traces
```

## What It Proves

The harness takes a snapshot after parsing and before reduction:

```text
L heap/code arena
a C c D U B b m u H I O scalars
input/output model positions
```

Then it runs forward, recording a residual entry for each irreversible-looking
operation. Then it consumes the residual log backward and checks:

```text
heap restored: yes
scalars restored: yes
output buffer empty: yes
input logically unconsumed: yes
round trip: yes
```

That is the concrete Bennett-style answer: every mutation has an inverse
because its old value is recorded.

## Phase Status

- Phase 0, standalone compile: done for the un-golfed BLC/Krivine core.
- Phase 1, residual log: done in `rlog.h`.
- Phase 2, instrument mutations: done for the exposed reducer mutations in
  `tromp.c`.
- Phase 3, free handling: done with deferred frees, not destructive free-list
  reuse.
- Phase 4, reversible I/O model: hooks are implemented for input-bit
  consumption, output-byte buffering, the ROM wrapper, the lazy input list,
  `wr0`/`wr1`, and bit/byte output behavior.
- Phase 5, backward driver: done in `run_backward()`.
- Phase 6, verification harness: done by direct heap/scalar comparison against
  the post-parse snapshot.

## What Is Logged

`rlog.h` defines the residual log. `tromp.c` currently logs:

- heap writes into the shared arena `L`
- scalar updates for `a`, `C`, `D`, `U`, `B`, `c`, `b`, `m`, `u`, `H`, `I`, `O`
- reference-count decrements
- allocation from the fresh heap frontier
- allocation from the free list, if a future exact-free mode enables it
- runtime input-bit consumption
- runtime output-byte buffering
- aggregate `Gro` expansion of the lazy input list
- `Put` bit accumulation and byte flushing
- termination

The reducer uses the clean Phase-3 strategy from the implementation plan:
reference-count frees are deferred during the forward run. So the free list
does not destructively reuse cells yet. This wastes heap space, but makes the
first reversible version easier to audit.

## Important Boundary

This is not `revblc.c` and it is not `native_beta.c`.

- `../revblc.c` is a separate readable Bennett baseline for a raw BLC-syntax
  Krivine reducer.
- `../native_beta.c` is the native-rule experiment for one reversible beta
  transition.
- `tromp.c` is the Tromp-answer track: un-golf the BLC/Krivine core, log
  each mutable operation, then verify restoration against a byte snapshot.

This version runs the raw BLC examples in this directory through the
Tromp/Justine runtime wrapper. The identity program `0010` is also included as
`examples/io/cat.blc`; under the wrapper it copies the lazy input list to the
`wr0`/`wr1` output continuations.

Packed `.Blc` input is supported. `examples/io/reverse.Blc` is vendored from
Justine's published program corpus and tested in byte mode. External
byte-for-byte parity is covered by the optional reference target:

```sh
git clone https://github.com/tromp/AIT /tmp/AIT
make -C /tmp/AIT uni
make test-reference
```

`make test-reference` compares this interpreter against Tromp's `uni` on a
packed identity/cat program and on `reverse.Blc`, while also checking that the
reversible run still reports `round trip: yes`.

## Example Output

```text
$ ./krivine_rev ../examples/identity_app.blc
krivine_rev: reversible instrumentation of BLC/Krivine core
mode: bit, prelude: on
program bits: 0100100010
runtime input bytes: 0
decoded root: (\.0 \.0)
snapshot: post-parse, pre-reduce

forward:
  steps: 13
  exit code: 0
  residual entries: 74
  current term: \.0
  output bytes buffered: 0

backward:
  heap restored: yes
  scalars restored: yes
  kLazy restored: yes
  output buffer empty: yes
  input logically unconsumed: yes
  round trip: yes
```
