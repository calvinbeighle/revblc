# I/O Examples

`cat.blc` is readable ASCII BLC for identity (`0010`). With the
Tromp/Justine runtime wrapper, identity copies the lazy input list to the
`wr0`/`wr1` output continuations.

`reverse.Blc` is the packed byte-mode reverse program from Justine's published
SectorLambda corpus:

```text
https://justine.lol/lambda/reverse.Blc
```

It is included here so the default test suite exercises a real packed `.Blc`
program rather than only local toy examples.
