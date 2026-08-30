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
| DSK | +3 disk | yes | EDSK parse; PLUS3DOS CODE inject; uPD765 READ DATA |
| MGT | +D disk | yes | Directory CODE / 48K snapshot inject |
| MDR | microdrive | yes | Record concat + tape-header CODE inject |
| FDI | Beta disk | yes | 256-byte sectors → TR-DOS catalog inject |
| UDI | Beta disk | partial | Uncompressed TR-DOS catalog only |
| D80/D40 | Didaktik | yes | TR-DOS catalog or MGT-like dirents |
| SPG | snapshot | yes | Unpacked pages; MLZ packed not loaded |
| CSW | tape pulses | detect | Use TAP/TZX |
| IPF | flux | detect | CAPS — not loaded |
| ZIP | archive | in-place | libzip; `zip#member` — no extract |

Collection: `/mnt/games` (extracted) and `/mnt/Games.zip` (STORE, 1.7G, 82729 files).
