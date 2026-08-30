# ZX Spectrum file formats (ZXEm)

Index of era formats the emulator understands. Dual-write with pmem
`project.zxem.format.*`.

| Format | Kind | Load | Notes |
|--------|------|------|--------|
| TAP | tape | yes | Standard blocks; CODE fast-load |
| TZX | tape | yes | 0x10/0x11/0x14 data; others skipped |
| Z80 | snapshot | yes | v1/v2/v3; pages 3–10 → banks 0–7 |
| SNA | snapshot | yes | 48K and 128K |
| SZX | snapshot | yes | Fuse ZXST; zlib RAM pages |
| SP | snapshot | yes | SNAP “SP” 48K |
| SLT | snapshot | yes | Z80 + trailing levels ignored |
| SCL | TR-DOS | yes | Fast-inject CODE files |
| TRD | TR-DOS | yes | Catalog + CODE inject |
| POK | cheat | `--pok` | N/M/Z/Y trainer |
| ROM | cart/ROM | yes | 16K or 32K |
| DCK | Timex dock | yes | 8K banks |
| DSK | +3 disk | detect | Needs +3 FDC |
| MGT | +D disk | detect | Needs G+DOS |
| MDR | microdrive | detect | Needs IF1 |
| FDI/UDI | Beta disk | detect | Use SCL/TRD |
| CSW | tape pulses | detect | Use TAP/TZX |
| ZIP | archive | in-place | libzip; `zip#member` — no extract |

Collection: `/mnt/games` (extracted) and `/mnt/Games.zip` (STORE, 1.7G, 82729 files).
