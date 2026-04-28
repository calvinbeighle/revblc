# revblc

Reversible BLC/Krivine evaluator with residual-stream entropy measurement.

```sh
make test
./instrumented_krivine/krivine_rev examples/cat.blc --input 0101 \
  --dump-residual /tmp/cat.rlog
python3 measure.py /tmp/cat.rlog
```

`instrumented_krivine/tromp.c` is an un-golfed standalone C version of
Tromp's IOCCC 2012 Krivine core (`tromp-krivine-ioccc2012.c`,
Public Domain) with residual logging around every reducer mutation.
Forward run records `(tag, addr, old)` triples; backward run consumes them
and verifies heap, scalars, `kLazy`, output buffer, and input position
are restored. Optional `make test-reference` does byte-for-byte parity
against Tromp's `uni`.

`revblc.c` is a smaller readable version on raw BLC syntax.
`native_beta.c` rewrites one beta step in native-witness style instead of
trace logging (erasure / linear / duplication witnesses).

## Residual entropy

Five-program corpus with `--dump-residual`:

| program      | entries |    raw | floor (b/e) | floor B | best off-shelf | gap × |
| ------------ | ------: | -----: | ----------: | ------: | -------------: | ----: |
| identity_app |      74 |   1480 |        4.73 |      44 |     214 (zstd) |  4.86 |
| k_i_i        |      90 |   1800 |        4.81 |      54 |     248 (zstd) |  4.59 |
| cat (bit)    |     863 |  17260 |        5.98 |     645 |    1356 (zstd) |  2.10 |
| cat (byte)   |    4515 |  90300 |        6.29 |    3550 |      5688 (xz) |  1.60 |
| reverse.Blc  |    8504 | 170080 |        6.62 |    7037 |     10164 (xz) |  1.44 |

Floor = zeroth-order joint entropy of `(tag, addr_delta, old_delta)` with
deltas taken per-tag. zstd `--ultra -22`; xz `-9e`. Tag distribution is
interpreter-shaped, not program-shaped (`heap ~28%`, `C/D ~15%`, `a/c
~10%` across all five). xz/zstd are within ~1.4× of the floor on long
logs, ~5× on short ones. The floor is not the actual lower bound: a coder
conditioning on machine state (`H`, `C`, `D`, ...) at each step would be
tighter. Not implemented.
