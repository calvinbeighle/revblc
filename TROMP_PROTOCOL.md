# Tromp / Justine Runtime Protocol

This note documents the runtime protocol implemented by
`instrumented_krivine/tromp.c`.

Canonical references:

- Justine Tunney, `lambda.c` / SectorLambda:
  `https://justine.lol/lambda/lambda.c`
- Justine's explanatory page:
  `https://justine.lol/lambda/`
- John Tromp's AIT `uni.c`, used as the SKI-graph cross-reference:
  `https://github.com/tromp/AIT/blob/master/uni.c`

## Runtime Shape

The reducer uses the same four opcodes as the un-golfed Krivine version:

```text
IOP = 0
VAR = 1
APP = 2
ABS = 3
```

Terms use the compact `lambda.c` layout:

```text
ABS body          => [ABS, body...]
VAR n             => [VAR, n]
APP fun arg       => [APP, span(fun), fun..., arg...]
IOP               => [IOP]
```

So an application stores the size of its function, not absolute child
pointers.

## ROM Wrapper

At startup, `tromp.c` loads the static ROM wrapper, then appends:

```text
APP <span(user-program)> <user-program> <input-thunk-at-END>
```

The machine starts at `C = 0`, not at the raw user program. That matches the
important runtime idea from Justine's page:

```text
user program + lazy input list + wr0/wr1 output continuations
```

The raw identity program `0010` therefore behaves like `cat` under the wrapper.

## Lazy Input

When control reaches `C == END`, the machine performs the input action that
Justine's `lambda.c` calls from `Iop()`.

On EOF it appends NIL:

```text
ABS ABS VAR 0
```

On a bit-mode input byte, it appends a 7-cell list node whose value is either
`FALSE` or `TRUE`, using the low bit of the byte. This is why ASCII `0` and
ASCII `1` work as bit-mode test input.

On byte-mode input, it appends a 7-cell node whose value points at `kLazy[c]`,
where `kLazy` is the precomputed 8-bit list for byte `c`.

The runtime uses aggregate reversal here:

```text
TAG_GRO_EXPAND(count, old_END)
```

`Gro` only extends the code arena. It never overwrites existing cells. Reverse
therefore restores `END` and zeroes the appended cells instead of logging every
new cell individually.

## Output

The output continuations are the ROM `IOP` slots:

```text
WR0 = 20
WR1 = 21
```

In bit mode, `Put` writes ASCII `0` or `1` directly.

In byte mode, `WR0` and `WR1` accumulate bits in `co`; the outer output layer
flushes `co` as one byte. The reversible log records both:

```text
TAG_PUT_BIT   restore old co
TAG_PUT_BYTE  restore old OUT_LEN
```

The public program output goes to stdout. Verification diagnostics go to
stderr, so tests can assert exact output bytes.

## Reversal Invariants

The verifier snapshots the state after ROM loading, byte-list construction,
program parsing, and wrapper setup. After forward execution, it consumes the
residual log backward and checks:

```text
heap/code arena restored
scalars restored
kLazy restored
output buffer empty
input logically unconsumed
```

The important new invariant is:

```text
Gro extends only; it does not overwrite.
```

That is what makes aggregate `TAG_GRO_EXPAND` valid.

`kLazy restored: yes` is intentionally a tripwire. The table is built before
the snapshot and should never be mutated during execution; failure means an
out-of-bounds or misaddressed write touched static runtime data.

`VAR` is the expensive transition. Walking a deep environment logs the scratch
state changes for `D` and `u`, so residual cost is proportional to the de
Bruijn index being looked up.

## Current Boundary

The runtime wrapper, lazy input list, bit-mode output, byte-mode output, and
`kLazy[256]` table are implemented.

Packed `.Blc` input is implemented. `examples/io/reverse.Blc` is vendored from
Justine's published program corpus and is tested in byte mode.

External reference parity is implemented as an optional test:

```sh
git clone https://github.com/tromp/AIT /tmp/AIT
make -C /tmp/AIT uni
cd revblc/instrumented_krivine
make test-reference
```

That target compares byte-for-byte output against Tromp's `uni` for packed
identity/cat and for `reverse.Blc`, and still requires the reversible pass to
report `round trip: yes`.
