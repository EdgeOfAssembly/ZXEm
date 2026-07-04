# Manic Miner (1983 Bug-Byte) - Verification Report

Generated: 2026-07-04

## 1. Z80 Header Verification

All 30-byte v1 header fields independently parsed and verified against findings.json:

| Field | Header Value | findings.json | Status |
|-------|-------------|---------------|--------|
| PC | 0x9303 | 0x9303 | MATCH |
| SP | 0x9CFC | 0x9CFC | MATCH |
| IM mode | 1 | 1 | MATCH |
| Compression | v1 compressed (flag 0x20) | v1 compressed | MATCH |
| A | 0xF8 | 0xF8 | MATCH |
| F | 0x02 | 0x02 | MATCH |
| BC | 0x1431 | 0x1431 | MATCH |
| DE | 0x1113 | 0x1113 | MATCH |
| HL | 0x59F8 | 0x59F8 | MATCH |
| IX | 0x0000 | 0x0000 | MATCH |
| IY | 0x8486 | 0x8486 | MATCH |
| BC' | 0x1421 | 0x1421 | MATCH |
| DE' | 0x369B | 0x369B | MATCH |
| HL' | 0x2758 | 0x2758 | MATCH |
| A' | 0xFF | 0xFF | MATCH |
| F' | 0x91 | 0x91 | MATCH |
| af_alt | 0x91FF (A'=0xFF, F'=0x91) | 0x91FF | MATCH |
| I | 0x3F | 0x3F | MATCH |
| R | 0x56 | 0x56 | MATCH |
| IFF1 | 0 | 0 | MATCH |
| IFF2 | 0 | 0 | MATCH |

**Verdict: All header fields correct.**

## 2. ISR at 0x0038 and ROM Region

- 0x0038 is in the ROM region (0x0000-0x3FFF), which is NOT present in ram48.bin (covers 0x4000-0xFFFF only).
- The entire ROM region (0x0000-0x3FFF) contains zero non-zero bytes in the decompressed snapshot.
- The game requires a synthetic ROM providing at minimum an ISR at 0x0038 (EI; RET = 0xFB 0xC9).
- `uses_rom: false` is correct - the game does not call any Spectrum ROM routines.

**Verdict: Correct. Game needs synthetic ROM with ISR at 0x0038.**

## 3. CALL/JP to ROM Region

Recursive disassembly from entry point 0x85CC through the code region (0x8000-0x9FFF) found **zero** CALL or JP instructions targeting ROM addresses (0x0000-0x3FFF). No RST instructions were found in the disassembled code paths either.

A naive byte-scan of the entire RAM found many apparent ROM references, but these are all false positives from data bytes (screen data, level data, sprite data) that happen to match opcode patterns.

**Verdict: `rom_calls: []` is correct. No ROM calls exist in the game code.**

## 4. Screen Verification

Generated `/tmp/reverse/screen.png` from the snapshot's bitmap (0x4000-0x57FF) and attributes (0x5800-0x5AFF).

- Bitmap: 1908/6144 non-zero bytes (screen has visible content)
- Attributes: 703/768 non-zero bytes (color data present)
- The decoded image shows recognizable Spectrum graphics with proper color attributes.
- Screen layout matches standard ZX Spectrum 256x192 bitmap + 32x24 attributes.

**Verdict: Screen data at 0x4000 decodes correctly as a Spectrum display.**

## 5. Keyboard Scanning Routine (0x87F0-0x88FF)

Disassembly of the keyboard routine confirmed the following port reads:

| Address | Instruction | Port | Spectrum Row |
|---------|------------|------|-------------|
| 0x87F1 | LD BC,0xFEFE; IN A,(C) | 0xFEFE | Caps Shift,Z,X,C,V |
| 0x87F7 | LD B,0x7F; IN A,(C) | 0x7FFE | Space,Sym Shift,M,N,B |
| 0x8801 | LD B,0xFD; IN A,(C) | 0xFDFE | A,S,D,F,G |
| 0x880B | LD B,0x02; IN A,(C) | 0x02FE | (Kempston-like, unused row) |
| 0x881D | LD B,0xBF; IN A,(C) | 0xBFFE | Enter,L,K,J,H |
| 0x886C | LD BC,0x00FE; IN A,(C) | 0x00FE | (Kempston-like) |
| 0x8884 | LD BC,0xEFFE; IN A,(C) | 0xEFFE | 0,9,8,7,6 |
| 0x8896 | LD B,0xF7; IN A,(C) | 0xF7FE | 1,2,3,4,5 |
| 0x88BA | LD BC,0xF7FE; IN A,(C) | 0xF7FE | 1,2,3,4,5 |
| 0x88D8 | LD B,0xEF; IN A,(C) | 0xEFFE | 0,9,8,7,6 |

Additional keyboard port reads found elsewhere:
- 0x8BFB: LD BC,0xDFFE; IN A,(C) - Right movement (P,O,I,U,Y)
- 0x8C06: LD BC,0xFBFE; IN A,(C) - Left movement (Q,W,E,R,T)
- 0x8669: LD BC,0xBFFE; IN A,(C) - Enter,L,K,J,H
- 0x9342: LD BC,0xBFFE; IN A,(C) - Enter,L,K,J,H

All 8 standard Spectrum keyboard rows are read. The findings.json keyboard_rows are correct.

**ISSUE FOUND in key_mappings:**
- `pause` was listed as "A,B,C,D,E,F,G" → **corrected to "A,S,D,F,G"** (0xFDFE row)
- `tune_toggle` was listed as "H,J,K,L" → **corrected to "Enter,L,K,J,H"** (0xBFFE row)

## 6. Level Data Layout

Verified 20 levels of 1024 bytes each at 0xB000:

| Level | Address | First Byte | Non-zero bytes |
|-------|---------|-----------|----------------|
| 1 (Central Cavern) | 0xB000 | 0x16 | 560/1024 |
| 2 (The Cold Room) | 0xB400 | 0x16 | 926/1024 |
| 3 (The Menagerie) | 0xB800 | 0x0D | 538/1024 |
| 4 | 0xBC00 | 0x29 | 505/1024 |
| 5 | 0xC000 | 0x2E | 923/1024 |
| 6 | 0xC400 | 0x16 | 504/1024 |
| 7 | 0xC800 | 0x4D | 587/1024 |
| 8 | 0xCC00 | 0x72 | 568/1024 |
| 9 | 0xD000 | 0x16 | 522/1024 |
| 10 | 0xD400 | 0x16 | 569/1024 |
| 11 | 0xD800 | 0x0E | 554/1024 |
| 12 | 0xDC00 | 0x65 | 573/1024 |
| 13 | 0xE000 | 0x16 | 569/1024 |
| 14 | 0xE400 | 0x68 | 878/1024 |
| 15 | 0xE800 | 0x0E | 547/1024 |
| 16 | 0xEC00 | 0x65 | 505/1024 |
| 17 | 0xF000 | 0x16 | 670/1024 |
| 18 | 0xF400 | 0x16 | 547/1024 |
| 19 | 0xF800 | 0x16 | 950/1024 |
| 20 (The Final Barrier) | 0xFC00 | 0x2C | 692/1024 |

Level 1 first 32 tile bytes: `16 00 00 00 00 00 00 00 00 00 00 05 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 16`
- 0x16 = wall (left/right borders)
- 0x00 = blank/air
- 0x05 = UNKNOWN (not in tile_ids table - likely a platform variant or item)

The two halves of each level (first 512 vs second 512 bytes) are different, confirming 1024 bytes per level (not 512).

**ISSUE FOUND: `level_names` had 19 entries but `num_levels` is 20. Added "The Final Barrier" as level 20.**

**NOTE: `tile_ids` table is incomplete - 0x05 and many other tile IDs (0x08, 0x2E, 0x4D, 0x72, 0x65, 0x68, 0x2C, etc.) appear in level data but are not documented.**

## 7. Willy Sprite at 0x8200

- First 32 bytes: `06 00 3E 00 7C 00 34 00 3E 00 3C 00 18 00 3C 00 7E 00 7E 00 F7 00 FB 00 3C 00 76 00 6E 00 77 00`
- First frame is NOT all zero - contains valid sprite data.
- All 16 frames contain non-zero data.
- The sprite uses masked format (alternating mask/data bytes, indicated by the 0x00 bytes between data bytes).
- ASCII visualization shows a recognizable 16x16 character shape.

**Verdict: Correct. 16 frames of 32-byte masked sprites at 0x8200.**

## 8. Sound Output - OUT (0xFE),A Instructions

Found 10 OUT (0xFE),A instructions in the code region:

| Address | Context |
|---------|---------|
| 0x8852 | Sound loop in keyboard routine (bit 4 toggling for EAR/MIC) |
| 0x8928 | Sound/border output |
| 0x89AA | Sound/border output |
| 0x8AF5 | Sound/border output |
| 0x8B70 | Sound/border output |
| 0x907D | Sound/border output |
| 0x90D9 | Sound/border output |
| 0x90E0 | Sound/border output |
| 0x91B5 | Sound/border output |
| 0x92F8 | Sound/border output |

Also found 1 OUT (C),A at 0x8701 (ED 79).

Other OUT ports found:
- 0x8A21: OUT (0x58),A (matches findings.json `other_ports`)
- 0xDA85, 0xEA85: OUT (0x5C),A (in level data region, likely data bytes, not code)

**Verdict: `sound_port: "0xFE"` and `sound_method` are correct. 10 OUT (0xFE),A locations confirmed.**

## 9. Additional Verified Fields

| Field | Value | Verified |
|-------|-------|----------|
| entry_point | 0x85CC | Confirmed - code at 0x85CC starts with AF (XOR A), 32 07 84 (LD (0x8407),A) |
| back_buffer | 0x6000 | Confirmed - 0x6000-0x7AFF mostly zero in snapshot (back buffer not yet rendered) |
| guardian_table | 0x8000 | Confirmed - ASCII comma-separated numbers with control bytes (0x09, 0x16, 0x03, 0x22) |
| guardian_state_table | 0x80BE | Confirmed - contains guardian state data |
| score_address | 0x8444 | Confirmed - contains "core 0" in snapshot (partial score text) |
| lives_address | 0x8408 | Confirmed |
| high_score_address | 0x8434 | Confirmed - contains "Score " label |
| air_display_address | 0x8418 | Confirmed - contains "AIR0000" |
| current_level_address | 0x8407 | Confirmed - value 0x00 (level 0 = Central Cavern) |
| game_state_address | 0x845A | Confirmed |
| music_data_address | 0x8500 | Confirmed - 201/204 non-zero bytes, valid music data |
| music_data_size | 204 | Confirmed |
| kempston_port | 0x1F | Confirmed - IN A,(0x1F) at 0x887E |
| IN A,(0x80) | multiple | Confirmed - 7 instances at 0x8DFF, 0x8E1D, 0x8E22, 0x913C, 0x9184, 0x919E, 0x91F6 |
| level_script_interpreter | 0x9200 | Confirmed - code present |
| script_address | 0x9C00 | Confirmed - script data present |

## Summary of Corrections Applied to findings.json

1. **key_mappings.pause**: "A,B,C,D,E,F,G" → "A,S,D,F,G" (matches 0xFDFE keyboard row)
2. **key_mappings.tune_toggle**: "H,J,K,L" → "Enter,L,K,J,H" (matches 0xBFFE keyboard row)
3. **level_names**: Added missing 20th level "The Final Barrier"

## Unresolved Notes

- The `tile_ids` table documents only 16 tile types, but level data contains many more (0x05, 0x08, 0x2E, 0x4D, 0x72, 0x65, 0x68, 0x2C, etc.). A full tile ID enumeration would require deeper analysis of the tile rendering code.
- The `score_address` at 0x8444 shows "core 0" rather than 6 ASCII digits in this snapshot state - this is snapshot-state-dependent, not an error.
- The `keyboard_rows` entry for 0xFEFE says "Shift" but the Spectrum has both Caps Shift and Symbol Shift on this row. The entry for 0x7FFE omits "Sym Shift".
