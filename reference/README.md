# References

Primary reference:

```text
tromp-krivine-ioccc2012.c
```

This is the John Tromp IOCCC 2012 Krivine machine that jartine pasted into
redbean `#lambda` on 2026-04-19 as the starting point for a reversible BLC
project.

Secondary readable cross-reference:

```text
https://rosettacode.org/wiki/Universal_Lambda_Machine#Phix
```

`rosettacode-phix.txt` is the extracted Phix implementation from that page.
The full HTML page is not vendored; keep the URL as the source of truth.

`revblc.c` follows the same `VAR`, `APP`, and `ABS` Krivine transition shapes,
then adds explicit residuals and backward execution.
