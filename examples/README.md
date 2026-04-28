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
