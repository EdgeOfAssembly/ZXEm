# EDSK / +3 `.dsk`

ZXEm parses CPC/+3 DSK (`EXTENDED` / `MV - CPC`), builds a CHS sector map, and:

1. Reconstructs CP/M files (dir track 0, or track 2 for sector IDs `0x41–0x49`).
2. Fast-injects `PLUS3DOS` type-3 CODE (128-byte header, load address at +18).
3. Mounts the map on a uPD765 subset (`2FFD` MSR, `3FFD` data) for `--model plus3` with a user `plus3-0..3.rom`.

Do not ship Amstrad +3 ROMs. This host can load `/usr/share/fuse/plus3-*.rom` at runtime.
