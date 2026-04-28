# Review request

I built a runnable C project for reversible Binary Lambda Calculus / Krivine
evaluation:

```text
https://github.com/calvinbeighle/revblc
```

It has three pieces:

1. `instrumented_krivine/`
   - reversible instrumentation of a BLC/Krivine evaluator;
   - runs forward, records residual state, runs backward;
   - verifies heap/scalars/lazy input/output restoration;
   - supports packed `.Blc` and byte-mode I/O;
   - `make test-reference` checks packed `cat` and `reverse.Blc` against Tromp
     `uni`.

2. `revblc.c`
   - a readable trace-reversible BLC/Krivine reducer;
   - shows exactly what residual information `APP`, `ABS`, and `VAR` need;
   - demonstrates compute-copy-uncompute over BLC syntax.

3. `native_beta.c`
   - an experiment in making one beta step natively reversible;
   - instead of storing the whole redex, it stores the collision witness:
     erased argument, linear occurrence boundary, or duplication group.

Run:

```sh
git clone https://github.com/calvinbeighle/revblc
cd revblc
make test
make test-reference
./native_beta examples/native_duplication.blc
```

The claim is not that this is a complete native reversible lambda calculus. The
trace reducers are the concrete baseline. `native_beta.c` is the experimental
step toward making the calculus rule itself reversible over visible extended
state.

I would appreciate review on:

- whether the residual accounting for `APP`, `ABS`, and especially `VAR` is
  correct;
- whether the restoration checks in `instrumented_krivine/` are meaningful;
- whether the native beta witness idea is sound or missing a case;
- what transition or rule should be made native-reversible next.
