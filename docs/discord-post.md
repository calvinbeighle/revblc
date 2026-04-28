# Discord Post Draft

I made a first cut at the reversible-BLC/Krivine idea Justine suggested. The
most literal piece is now in `tromp_reversible/`.

That directory takes the Tromp/Krivine transition structure, un-golfs it into
standalone C, logs every reducer mutation, then runs the residual log backward
and verifies the post-parse heap/scalars are restored:

```sh
cd tromp_reversible
make test
./tromp_rev examples/identity_app.blc
./tromp_rev examples/io/cat.blc --input 0101
./tromp_rev examples/io/cat.blc --byte --input A
```

Example result:

```text
heap restored: yes
scalars restored: yes
kLazy restored: yes
output buffer empty: yes
input logically unconsumed: yes
round trip: yes
```

Scope note: this is the Bennett-style trace-reversal baseline, not a native
no-log reversible lambda calculus. The Tromp/Justine runtime wrapper is now in
place for readable ASCII BLC source: `wr0`/`wr1`, lazy input-list expansion,
bit-mode output, byte-mode output, and the `kLazy[256]` byte table. The
packed `.Blc` loader is also present and currently runs Justine's `reverse.Blc`
in byte mode. There is also an optional `make test-reference` target that
compares packed cat and `reverse.Blc` byte-for-byte against Tromp's `uni`.

The older `revblc.c` workbench remains useful as a smaller readable baseline:
it takes the VAR / APP / ABS Krivine transition shapes, records explicit
residual state, copies the weak-head result closure, then uncomputes the trace.

Build:

```sh
cc -std=c89 -Wall -Wextra -pedantic -O2 -o revblc revblc.c
./revblc examples/identity_app.blc
```

Example:

```text
input: 0100100010
decoded: (\.0 \.0)

forward:
  app  push argument closure; continue with function; save app marker
  abs  pop argument into environment; continue with body; save abs marker
  var  replace variable by environment closure; save index and old environment

result:
  closure: \.0 {env_depth=0}
  blc projection: 0010

uncompute:
  var -> abs -> app

restored original state after uncompute: yes
copied output retained: \.0 {env_depth=0}
```

I also added a smaller native-rule demo:

```sh
cc -std=c89 -Wall -Wextra -pedantic -O2 -o native_beta native_beta.c
./native_beta examples/native_duplication.blc
```

That one applies a single top-level beta rule over an extended reversible
state. It does not store the whole redex. It stores only the collision witness:
erased argument for erasure, occurrence boundary for linear use, or occurrence
group for duplication.

The interesting split is:

- Krivine trace baseline: `APP` and `ABS` only need markers; `VAR` must
  preserve lookup context.
- Native beta rule: the residual belongs to the calculus rule itself, so beta
  becomes a bijection on the visible extended state.
