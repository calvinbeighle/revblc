# Examples

`identity.blc`

```text
0010
```

This is `\.0`.

`identity_app.blc`

```text
0100100010
```

This is `((\.0) (\.0))`, the same small example Justine uses around `lam2bin`.

`k_i_i.blc`

```text
0101000011000100010
```

This is `(((\.\\.1) (\.0)) (\.0))`, a small constant-function example.

`closure_env.blc`

```text
0100001100010
```

This is `((\.\\.1) (\.0))`. It halts at a closure whose term projection is
`\.1` but whose environment is non-empty. This example exists to keep the
trace honest about closures instead of pretending the bare term is the whole
result.

`native_erasure.blc`

```text
010000100010
```

This is `((\.\\.0) (\.0))`. The outer bound variable is erased by beta, so
`native_beta` must carry the erased argument as the reversible witness.

`native_duplication.blc`

```text
01000110100010
```

This is `((\.(0 0)) (\.0))`. The bound variable is duplicated by beta, so
`native_beta` carries the group of occurrence paths that fold the copies back
into one argument.


`omega.blc`

```text
010001101000011010
```

This is the usual non-terminating omega example. The tests run it with a step
limit to verify partial traces can still be reversed.

## I/O examples

`io/cat.blc` is readable ASCII BLC for identity (`0010`). With the runtime
wrapper, identity copies the lazy input list to the output continuations.

`io/reverse.Blc` is a packed byte-mode reverse program used so the tests cover
a real packed `.Blc` input, not only local readable examples.
