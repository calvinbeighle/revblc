# References

Primary reference:

```text
tromp-krivine-ioccc2012.c
```

This is John Tromp's IOCCC 2012 Krivine machine, kept here as the reference
anchor for the BLC/Krivine transition structure.

Secondary readable cross-reference:

```text
https://rosettacode.org/wiki/Universal_Lambda_Machine#Phix
```

`rosettacode-phix.txt` is the extracted Phix implementation from that page.
The full HTML page is not vendored; keep the URL as the source of truth.

`revblc.c` follows the same `VAR`, `APP`, and `ABS` Krivine transition shapes,
then adds explicit residuals and backward execution.
