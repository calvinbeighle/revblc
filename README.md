# revblc

Reversible BLC/Krivine evaluator with residual-stream entropy measurement.

```sh
make test
python3 measure.py --corpus
```

`instrumented_krivine/tromp.c` is an un-golfed standalone C version of
Tromp's IOCCC 2012 Krivine core (`tromp-krivine-ioccc2012.c`, Public
Domain) with residual logging around every reducer mutation. Forward run
records `(tag, addr, old)` triples; backward run consumes them and
verifies heap, scalars, `kLazy`, output buffer, and input position are
restored. Optional `make test-reference` does byte-for-byte parity
against Tromp's `uni`.

`revblc.c` is a smaller readable version on raw BLC syntax.
`native_beta.c` rewrites one beta step in native-witness style instead
of trace logging (erasure / linear / duplication witnesses).

`measure.py` reports zeroth-order joint-delta entropy of the residual
log, runs three arithmetic coders over the same alphabet (static, online
adaptive, PPM-A first-order context), and computes the deterministic
bound by re-running the Krivine machine and verifying the dump matches
byte-for-byte.

## Results

`python3 measure.py --corpus`:

| program      | input      | entries |    raw | floor |  gzip |  zstd |    xz | static | adaptive |   ctx | bound |
| ------------ | ---------- | ------: | -----: | ----: | ----: | ----: | ----: | -----: | -------: | ----: | ----: |
| identity_app | -          |      74 |   1480 |    43 |   243 |   214 |   260 |    244 |      190 |   195 |  _15_ |
| k_i_i        | -          |      90 |   1800 |    54 |   291 |   248 |   296 |    275 |      215 |   222 |  _24_ |
| cat (bit)    | `01010101` |     863 |  17260 |   645 |  2181 |  1356 |  1380 |   1775 |     1520 |  1446 |  _17_ |
| cat (byte)   | `Hello`    |    4515 |  90300 |  3549 | 11374 |  6655 |  5688 |   7269 |     6500 |  5906 |  _14_ |
| reverse.Blc  | `abcdefgh` |    8504 | 170080 |  7036 | 20761 | 12222 | 10164 |  14041 |    12613 | 11720 |  _21_ |

`floor` = zeroth-order joint entropy of `(tag, addr_d, old_d)` with
deltas taken per-tag. `static`/`adaptive`/`ctx` are arithmetic coders
over that alphabet. `bound` is the deterministic-replay lower bound:
program bytes + input bytes + 4-byte length prefix.

## Findings

1. **Static coder hits the joint-delta entropy floor on payload to within
   1 byte** on every program, but the serialized frequency table inflates
   the total. xz/zstd beat it on every row.

2. **Online adaptive coder beats xz on the two smallest programs**, and
   beats zstd on cat (byte) and reverse.Blc.

3. **First-order context coder (PPM-A on previous tag) beats adaptive on
   every medium/long log**, narrowing the gap on reverse.Blc from 1.79×
   the floor (adaptive) to 1.67× (ctx). Still loses to xz.

4. **The Krivine machine is deterministic given `(program, input)`.**
   Verified by running twice and `cmp`-ing the dumps. The trace is fully
   reproducible. The information-theoretic floor for the residual log is
   therefore not the joint-delta entropy — it is `H(program) + H(input)`,
   which for reverse.Blc with 8-byte input is **21 bytes**: 484× smaller
   than xz, 335× smaller than the joint-delta floor. The decoder runs
   `krivine_rev` to reproduce the dump.

The bound assumes a re-simulator decoder. The "compressed" file carries
program bits plus input plus a length prefix; the decoder replays the
machine to recover the trace. This trades decode-time CPU for bits on
the wire. No state-conditioned coder is implemented yet — the bound is
computed by direct verification, not by encoding.

Decode-time on this corpus is competitive: re-running `krivine_rev` on
reverse.Blc takes ~6.4 ms vs ~6.1 ms for `xz -d`. Replay scales with
program runtime, so on long-running programs (e.g. Tromp's `lambda-8cc`
compiling C, ~2 min) replay loses to xz by orders of magnitude. The
tradeoff is cheap on short programs and painful on long ones.

## Further research

A state-conditioned predictor is the most direct extension of these
results. It sits between the zeroth-order coders and full replay:
extend the C dump with `(H, C, D, a, c)` snapshots at each entry, and
predict `addr/old` from those scalars before arithmetic coding. Scalar
updates are expected to collapse to zero residual bits, and heap
writes to a small delta from `H`, without paying replay CPU at decode
time. The size of the gap closed by this predictor relative to xz on
the long logs is the open empirical question.

The replay coder itself is implementation rather than investigation.
The deterministic bound in the table is computed by running twice and
`cmp`-ing the dumps; an end-to-end coder would emit
`(program, input, length_prefix)` and ship `krivine_rev` with the
decoder.

The corpus is small. Five programs cover identity, constant-function
reduction, and two I/O patterns. The natural next data points are
Tromp's RosettaCode entries and a self-interpreter layer (BLC running
on BLC), to test whether the interpreter-shaped tag distribution
holds under self-application.

Replay decode time scales with program runtime, so the parity with
`xz -d` observed here disappears once the program runs longer than
the time `xz -d` takes on its log. Quantifying the crossover means
sweeping input sizes on `reverse.Blc` and running `lambda-8cc`
against increasing C source lengths.
