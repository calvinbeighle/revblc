# Trace-Reversible Krivine Notes

This describes the Bennett-style baseline implemented in `revblc.c`: save
explicit residuals, copy the weak-head result closure, then run backward.

It is not a native no-log reversible lambda calculus.

The normal Krivine machine state is roughly:

```text
control closure + argument stack
```

where a closure is:

```text
term + environment
```

The useful reversible version adds:

```text
residual stack
```

Each forward step pushes exactly the information needed by the matching
backward step.

## APP

Forward:

```text
control = (f x, env)
stack   = s

control = (f, env)
stack   = (x, env) :: s
resid  += app
```

Backward:

```text
control = (f, env)
stack   = (x, env) :: s
resid   = app :: r

control = (f x, env)
stack   = s
resid   = r
```

Only a marker is needed because the argument closure is still on the stack.

## ABS

Forward:

```text
control = (\.body, env)
stack   = arg :: s

control = (body, arg :: env)
stack   = s
resid  += abs
```

Backward:

```text
control = (body, arg :: env)
resid   = abs :: r

control = (\.body, env)
stack   = arg :: s
resid   = r
```

Only a marker is needed because the argument is now at the head of the
environment.

## VAR

Forward:

```text
control = (var i, env)
target  = env[i]

control = target
resid  += var(i, env)
```

Backward:

```text
control = target
resid   = var(i, env) :: r

control = (var i, env)
resid   = r
```

This is the expensive step. Variable lookup overwrites the old control closure,
so the residual must preserve the old variable index and environment.
