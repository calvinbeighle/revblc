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
log and runs two arithmetic coders over the same alphabet: a static
two-pass coder with a serialized frequency table, and an online adaptive
coder (Method A: ESCAPE + literal varint for new symbols).

## Residual entropy and custom coders

| program      | input      | entries |    raw | floor B |  gzip |  zstd |    xz |    static (hdr+pay) | adaptive |
| ------------ | ---------- | ------: | -----: | ------: | ----: | ----: | ----: | ------------------: | -------: |
| identity_app | -          |      74 |   1480 |      44 |   243 |   214 |   260 |      244 = 200 + 44 |  **190** |
| k_i_i        | -          |      90 |   1800 |      54 |   291 |   248 |   296 |      275 = 220 + 55 |  **215** |
| cat (bit)    | `01010101` |     863 |  17260 |     645 |  2181 |  1356 |  1380 |   1775 = 1129 + 646 |     1520 |
| cat (byte)   | `Hello`    |    4515 |  90300 |    3550 | 11374 |  6655 |  5688 |  7269 = 3719 + 3550 |     6500 |
| reverse.Blc  | `abcdefgh` |    8504 | 170080 |    7037 | 20761 | 12222 | 10164 | 14041 = 7004 + 7037 |    12613 |

Floor = zeroth-order joint entropy of `(tag, addr_delta, old_delta)` with
deltas taken per-tag. zstd `--ultra -22`; xz `-9e`. Bold = beats every
off-the-shelf compressor.

Tag distribution is interpreter-shaped, not program-shaped (`heap ~28%`,
`C/D ~15%`, `a/c ~10%` across all five).

## Did the custom coders close the gap?

Partly. Three findings:

1. **Static coder hits the entropy floor on payload to within 1 byte**
   on every program, but its serialized frequency table inflates the
   total. xz/zstd beat it on every row.

2. **Adaptive coder beats zstd on three programs and beats xz on the two
   smallest.** Removing the header is enough to win on short logs; the
   crossover with xz happens around ~1000 residual entries.

3. **xz still wins on the three longest logs.** Best ratio achieved on
   reverse.Blc: 10164 B (xz) vs 7037 B floor — gap ~1.44×. Adaptive on
   the same log: 12613 B, gap ~1.79×.

The remaining gap is the price of a zeroth-order model. Both custom
coders encode each symbol independently. xz's LZMA captures sequence
structure (e.g. `TAG_HEAP` is frequently followed by `TAG_a`). Closing
that gap requires either first-order context modeling (`P(d | prev_d)`)
or a state-conditioned predictor that uses the machine state (`H`, `C`,
`D`, ...) at each step. Both unimplemented.
