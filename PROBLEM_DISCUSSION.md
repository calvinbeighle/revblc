# Problem discussion

Source: redbean Discord, `#topics/lambda`, 2026-04-19 / 2026-04-20.

## The challenge

Justine, to Lewpen, after Lewpen described reversible computing in the abstract:

> I want you to change Tromp's lambda calculus interpreter so that everything
> it does is reversible.

Reference code Justine pasted: Tromp's IOCCC 2012 obfuscated Krivine machine,
plus Justine's de-obfuscated `https://justine.lol/lambda/lambda.c` (known to
have a subtle bug Justine never tracked down).

## Technical context Lewpen raised

- `AND` outputs ~0.81 bits of entropy when inputs are independent and uniform
  (`P(out=1) = 1/4`, binary entropy ≈ 0.81).
- A reversible AND therefore needs ~1.19 bits of side information per op,
  not the trivial 2 bits ([A,B] copy).
- ahgamut produced the working baseline:
  ```python
  def Bdash(A, B, Adash):
      out = []
      for i in range(len(A)):
          if Adash[i]: continue
          out.append(A[i])
          if A[i]: continue
          out.append(B[i])
      return out
  ```
  → 1.25 bits/AND. Theoretical floor 1.19. Gap closes only with arithmetic
  coding over the residual stream.
- Justine's framing: reversible computing and erasure codes are two sides of
  the same coin. Forward direction preserves enough side info to undo;
  channel-coding direction adds enough redundancy to undo corruption.

## What "reversible Krivine" actually means

A Krivine step is one of:

- `Var n` — index into the environment, push a closure onto the stack;
- `App` — push the right subterm (with current env) onto the stack;
- `Lam` — pop the stack into the environment, descend into the body.

Each step destroys information:

- `App` discards the application node.
- `Lam` consumes a stack frame.
- `Var` substitutes a closure into the focus.

To run the machine backwards, the inverse of each step needs whatever was
overwritten or popped. Three open questions:

1. **What is the minimum residual per step?** Trivial answer: log the popped
   stack frame and the discarded subterm pointer. Less trivial: the residual
   has structure (e.g. `App` always pops what `Lam` pushed earlier under
   matching brackets).
2. **Is residual structure compressible the way AND's is?** I.e. is there a
   Krivine-step entropy lower than the naive log-everything bound?
3. **Does the BLC self-interpreter's CPS shape help or hurt?** Tromp noted
   the BLC self-interpreter is CPS; CPS uses constant closures vs. linear in
   direct style (woodrush/tromp, Oct 2022). Whether that helps reversibility
   is open.

## Adjacent work that already exists

- Janet (the reversible language) — reachable but apparently can't even
  Fibonacci comfortably (Lewpen's survey).
- Reversible parsing vs. pretty-printing literature.
- Bennett 1973 (the foundational reversible-TM paper).
- HVM / interaction nets — beta is local and the substrate is closer to
  reversible naturally; HVM2 is what HOC ships.

## Status of this repo

The challenge is open. We are not at expert level. Build minimum viable
reversible Krivine, then ask whether the residual stream has the same
"fractional bit" compressibility AND does.
