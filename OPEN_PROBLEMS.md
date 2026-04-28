# Open Problems

These are the places where the toy becomes interesting.

## 0. External Interpreter Parity

`instrumented_krivine/tromp.c` is now the literal-answer track: it is an un-golfed,
standalone BLC/Krivine-core derivative with residual logging and backward
verification.

The Tromp/Justine runtime wrapper is now present: the user program is applied
to the lazy input list and `wr0`/`wr1` output continuations; `Gro` extends the
input list; `Put` handles bit-mode ASCII output and byte-mode accumulation; and
the reverse pass restores the heap, scalars, `kLazy`, input position, and
output buffer.

Packed `.Blc` parsing is implemented too, and `reverse.Blc` from Justine's
published program corpus is tested in byte mode.

External parity against Tromp's `uni` is now reproducible with:

```sh
git clone https://github.com/tromp/AIT /tmp/AIT
make -C /tmp/AIT uni
cd revblc/instrumented_krivine
make test-reference
```

The current target compares packed identity/cat and `reverse.Blc` byte-for-byte
against `uni`, then requires the reversible run to report `round trip: yes`.
The remaining compatibility work is broader corpus coverage, not first
reference parity.

## 1. Residual Cost

`APP` and `ABS` only need markers. `VAR` stores more context.

Can a different environment representation make variable lookup cheaper to
reverse?

## 2. Full Tromp Compatibility

`revblc` parses pure BLC terms and evaluates them as lambda terms. Tromp's
universal machine also handles input/output encodings.

The next compatibility target is to run the same small examples as the
RosettaCode Universal Lambda Machine.

## 3. Smaller C

The current C is deliberately readable.

A fun follow-up is a smaller version that keeps the same reversible trace but
looks more like the compact BLC interpreters in the channel.

## 4. Native Reversible Calculus

The current program is Bennett-style trace reversibility. That is the boring
baseline: log enough information, copy the result, run backward.

`native_beta.c` implements one native reversible beta rule over an extended
visible state. It is useful because it shows the right information accounting:
erasure carries the erased argument, linear use carries an occurrence boundary,
and duplication carries an occurrence group.

The harder goal is a complete native reversible binary calculus where
all reduction rules are bijections on the visible term/state and do not rely on
a separate trace stack. That is not implemented here yet.

## 5. Native Krivine Synthesis

`revblc.c` and `native_beta.c` do not talk to each other yet.

That is an intentional staging point, not the final architecture:

- `revblc.c` shows the Krivine machine transitions and the Bennett-style
  residual cost of reversing them.
- `native_beta.c` shows one native reversible beta rule with a visible
  collision witness instead of a side trace.

The natural synthesis is a Krivine machine whose `APP`, `ABS`, and `VAR`
transitions each carry native reversibility witnesses analogous to the beta
cases. In Krivine form, the hard part is not quite the same as small-step beta:
substitution is delayed, so erasure and duplication pressure shows up around
environment use and `VAR` lookup.

That synthesis has not been started. A useful next milestone would be one
Krivine transition rewritten in this native-witness style, then compared
against the current Bennett residual for the same example.
