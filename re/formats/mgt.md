# MGT / +D `.mgt`

819200-byte DISCiPLE/+D image: 80 cyl × 2 sides × 10 × 512.

Directory: 80 × 256-byte entries at the start of the file.

| Byte | Meaning |
|------|---------|
| 0 | type (low 6 bits): 1 BASIC, 4 CODE, 5 48K snap, 7 SCREEN$, 9 128K snap |
| 1–10 | name |
| 11–12 | sector count (big-endian) |
| 13 | start track |
| 14 | start sector (1–10) |
| ~210 | ZX header (type/length/start); ZXEm also accepts a 1-byte hole before length |

Layout: `offset = ((track*2+side)*10+(sector-1))*512`.
