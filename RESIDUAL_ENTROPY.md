# Residual entropy

`instrumented_krivine` logs each forward step as `(tag, addr, old)`: the kind
of mutation, where it happened, and what value it overwrote. Bennett-style
reversal replays this log backward. Question: how compressible is the log?

## Method

`krivine_rev --dump-residual FILE` writes the residual log as a fixed-width
`RLOG0001`-magic file (20-byte records: `i32 i64 i64`). `tools/measure.py`
reports:

```
raw_bytes              20 * count
H(tag)                 zeroth-order entropy of the tag column
H(addr_d | tag)        conditional entropy of per-tag addr deltas
H(old_d  | tag)        conditional entropy of per-tag old deltas
joint-delta floor      H(tag, addr_d, old_d), zeroth-order over per-tag deltas
gzip / zstd / xz       off-the-shelf compressor sizes on the raw record body
```

Per-tag deltas: for each tag, store consecutive entries' `addr`/`old` as
differences from the previous entry of the same tag. Equivalent to a coder
that maintains per-tag last-value state.

## Corpus and results

| program      | entries |    raw | floor b/e | floor B | best off-shelf | gap × |
| ------------ | ------: | -----: | --------: | ------: | -------------: | ----: |
| identity_app |      74 |   1480 |      4.73 |      44 |     214 (zstd) |  4.86 |
| k_i_i        |      90 |   1800 |      4.81 |      54 |     248 (zstd) |  4.59 |
| cat (bit)    |     863 |  17260 |      5.98 |     645 |    1356 (zstd) |  2.10 |
| cat (byte)   |    4515 |  90300 |      6.29 |    3550 |      5688 (xz) |  1.60 |
| reverse.Blc  |    8504 | 170080 |      6.62 |    7037 |     10164 (xz) |  1.44 |

`gap × = best / floor`. zstd is `--ultra -22`; xz is `-9e`.

## Findings

1. **Tag distribution is interpreter-shaped, not program-shaped.** Across all
   five programs: `heap ~28%`, `C/D ~15%`, `a/c ~10%`, `alloc_fresh/H ~5%`.
   The shape comes from the Krivine machine, not the workload.

2. **Naive 20-byte records are ~25× above the zeroth-order entropy floor**
   (`floor_B / raw` is 0.030–0.041 across the corpus).

3. **xz/zstd close to within ~1.4× of the floor on long logs**, ~5× on short
   ones. Gap shrinks with log length. By ~10 KB residual log, off-the-shelf
   compression is already near the zeroth-order limit.

## Caveats

- **The floor is zeroth-order over per-tag deltas.** A coder conditioning on
  machine state (`H`, `C`, `D`, ...) at each step would produce a tighter
  bound. Many `TAG_HEAP addr` values are fully predictable from the heap
  pointer at that moment. Not implemented.

- **Tiny logs bias the floor estimate low.** identity_app has 74 entries with
  ~70 unique triples; the floor calculation does not charge for transmitting
  the symbol table. The two longest rows of the table are the honest ones.

## Reproduce

```sh
cd instrumented_krivine && make krivine_rev && cd ..
./instrumented_krivine/krivine_rev examples/identity_app.blc \
  --dump-residual /tmp/ia.rlog
./instrumented_krivine/krivine_rev examples/io/reverse.Blc --byte \
  --input abcdefgh --dump-residual /tmp/rev.rlog
python3 tools/measure.py /tmp/ia.rlog /tmp/rev.rlog
```
