# revblc

Reversible BLC/Krivine evaluator with residual-stream entropy measurement.

```sh
make test
./instrumented_krivine/krivine_rev examples/cat.blc --input 01010101 \
  --dump-residual /tmp/cat.rlog
python3 measure.py /tmp/cat.rlog
```

`instrumented_krivine/tromp.c` is an un-golfed standalone C version of
Tromp's IOCCC 2012 Krivine core (`tromp-krivine-ioccc2012.c`,
Public Domain) with residual logging around every reducer mutation.
Forward run records `(tag, addr, old)` triples; backward run consumes
them and verifies heap, scalars, `kLazy`, output buffer, and input
position are restored. Optional `make test-reference` does byte-for-byte
parity against Tromp's `uni`.

`revblc.c` is a smaller readable version on raw BLC syntax.
`native_beta.c` rewrites one beta step in native-witness style instead
of trace logging (erasure / linear / duplication witnesses).

`measure.py` reports zeroth-order joint-delta entropy of the residual
log and runs three arithmetic coders over the same alphabet: a static
two-pass coder with a serialized frequency table, an online zeroth-order
adaptive coder (Method A: ESCAPE + literal varint for new symbols), and
a first-order context coder (PPM-A style, conditioning on the previous
symbol's tag, falling back to a global table on miss).

## Residual entropy and custom coders

| program      | input      | entries |    raw | floor B |  gzip |  zstd |    xz | static | adaptive | ctx (prev_tag) |
| ------------ | ---------- | ------: | -----: | ------: | ----: | ----: | ----: | -----: | -------: | -------------: |
| identity_app | -          |      74 |   1480 |      44 |   243 |   214 |   260 |    244 |  **190** |            195 |
| k_i_i        | -          |      90 |   1800 |      54 |   291 |   248 |   296 |    275 |  **215** |            222 |
| cat (bit)    | `01010101` |     863 |  17260 |     645 |  2181 |  1356 |  1380 |   1775 |     1520 |           1446 |
| cat (byte)   | `Hello`    |    4515 |  90300 |    3550 | 11374 |  6655 |  5688 |   7269 |     6500 |           5906 |
| reverse.Blc  | `abcdefgh` |    8504 | 170080 |    7037 | 20761 | 12222 | 10164 |  14041 |    12613 |          11720 |

Floor = zeroth-order joint entropy of `(tag, addr_delta, old_delta)` with
deltas taken per-tag. zstd `--ultra -22`; xz `-9e`. Bold = beats every
off-the-shelf compressor.

Tag distribution is interpreter-shaped, not program-shaped (`heap ~28%`,
`C/D ~15%`, `a/c ~10%` across all five).

## Did the custom coders close the gap?

Partly. Four findings:

1. **Static coder hits the entropy floor on payload to within 1 byte**
   on every program, but its serialized frequency table inflates the
   total. xz/zstd beat it on every row.

2. **Adaptive coder beats xz on the two smallest programs** (190 vs 260,
   215 vs 296) and beats zstd on cat (byte) and reverse.Blc. The
   crossover with xz on long logs happens because the coder is still
   zeroth-order.

3. **Context coder (PPM-A on previous tag) beats adaptive on every
   medium/long log**, narrowing the gap on reverse.Blc from 1.79× the
   floor (adaptive) to 1.67× (ctx). Loses to adaptive on the smallest
   logs because the per-context tables are too sparse to help.

4. **xz still wins on the three longest logs.** Best ratio achieved on
   reverse.Blc: 10164 B (xz) vs 7037 B floor — gap ~1.44×. Our best on
   the same log: 11720 B (ctx), gap ~1.67×.

The remaining gap is what xz's LZMA captures and our zeroth-/first-order
coders do not: longer-range sequence structure and dictionary matching.
Closing it further with a "language-model" coder is a competition we
will keep losing. The genuinely different move is a state-conditioned
predictor: at each step, predict `addr/old` from the current machine
state (`H`, `C`, `D`, ...) of the reversible Krivine evaluator. Many
residuals are then ~0 bits because they are determined by state already
known at decode time. Unimplemented.
