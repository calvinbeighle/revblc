# Native Reversible Beta Rule

`native_beta.c` demonstrates one native reversible reduction rule on real
BLC-encoded lambda terms.

It is deliberately smaller than a full reversible BLC machine:

```text
closed top-level beta redex
  -> one reversible beta state
  -> reconstruct original redex
```

The point is to avoid the boring construction:

```text
reduct + full original redex
```

That would be reversible, but it would just embed the input in the output.
Instead the native state carries only the witness needed for the collision
caused by beta.

## Rule Shape

Ordinary beta:

```text
((\ body) arg) -> body[arg / 0]
```

Native reversible beta:

```text
((\ body) arg) <-> beta_witness(reduct, witness)
```

where:

```text
reduct = body[arg / 0]
```

The witness depends on how many times the bound variable occurs in `body`.

## Erasure

If the bound variable occurs zero times, beta erased the argument:

```text
((\. \.0) (\.0)) -> \.0
```

The reversible state must carry the erased argument:

```text
erasure_beta(reduct=\.0, erased_arg=\.0)
```

It does not need to carry the whole redex.

## Linear Use

If the bound variable occurs exactly once, no argument term is erased and no
copy is made.

For raw unlabelled BLC syntax, the inverse still needs to know the occurrence
boundary: which subtree of the reduct came from the original argument.

```text
((\.0) (\.0)) -> \.0
linear_beta(reduct=\.0, paths=[.@depth0])
```

So the term payload is zero, but the demo keeps an occurrence path as visible
state. If a future calculus uses focused terms or labelled sharing, this path
can become part of the term representation rather than an auxiliary witness.

## Duplication

If the bound variable occurs more than once, beta makes multiple copies:

```text
((\.(0 0)) (\.0)) -> ((\.0) (\.0))
```

The reversible state does not store another copy of the argument. The reduct
already contains the copies. The witness stores the group of occurrence paths
that must fold back into one original argument:

```text
duplication_beta(reduct=(\.0 \.0), paths=[L@depth0, R@depth0])
```

Backward reconstruction verifies that all grouped paths still contain equal
terms before rebuilding the original redex.

## Why This Matters

This is closer to the redbean `#lambda` discussion than a step logger because
the residual is about the calculus rule itself. The visible state changes so
the beta transition is a bijection.

It is still only one rule. A complete native reversible BLC/Krivine machine
would need every machine transition to have this kind of accounting, including
the Krivine `VAR` step where lookup overwrites the current closure.
