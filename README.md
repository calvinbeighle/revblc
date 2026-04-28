# revblc

Reversible execution experiments for Binary Lambda Calculus.

The driving question: what information must a BLC/Krivine evaluator preserve
to be runnable backward, and how compressible is that information in
practice?

- `PROBLEM_DISCUSSION.md` — the original challenge from the redbean #lambda
  channel.
- `RESIDUAL_ENTROPY.md` — the empirical answer: corpus measurement of the
  residual log.
- `TROMP_PROTOCOL.md` — runtime details of `instrumented_krivine` (lazy
  input, byte-mode output, packed `.Blc` parsing, reference parity).

## Build and run

```sh
make test
make test-reference   # requires Tromp's uni at /tmp/AIT
```

## Three artifacts

`revblc.c` — readable Bennett-style trace-reversible Krivine reducer. `APP`
and `ABS` need only markers; `VAR` must preserve the lookup context that
gets overwritten.

`native_beta.c` — one beta step made reversible at the calculus level
instead of the trace level. Witnesses split by occurrence count of the bound
variable: erasure carries the erased argument; linear carries an occurrence
boundary; duplication carries the occurrence group.

`instrumented_krivine/` — fuller evaluator with the Tromp/Justine runtime
wrapper, lazy input, byte-mode output, packed `.Blc`, and parity against
Tromp's `uni`. The residual log used in `RESIDUAL_ENTROPY.md` comes from
this artifact.

## Residual measurement

```sh
./instrumented_krivine/krivine_rev examples/io/cat.blc --input 0101 \
  --dump-residual /tmp/cat.rlog
python3 tools/measure.py /tmp/cat.rlog
```

## Scope

Bennett-style trace reversibility is well known. The piece that may be new
is the empirical residual-entropy analysis in `RESIDUAL_ENTROPY.md`. The
trace reducers and `native_beta.c` are baselines for that measurement.

`native_beta.c` rewrites one rule in native-witness style; `revblc.c` and
`instrumented_krivine` still log a side trace. Synthesizing the two — a
Krivine machine whose `APP`, `ABS`, and `VAR` carry native witnesses — has
not been started.
