# Manic Miner (1983 Bug-Byte) - Memory Map

## Overview
- **Snapshot**: Z80 v1 compressed 48K
- **PC at snapshot**: 0x9303
- **SP at snapshot**: 0x9CFC
- **IM**: 1
- **Entry point**: 0x85CC (after ROM init, game starts here)
- **ROM**: Not present in snapshot; game uses a synthetic ROM at 0x0000-0x3FFF with `EI; RET` at 0x0038

## Memory Layout

### 0x0000 - 0x3FFF: ROM (not in snapshot)
- 0x0038: IM1 interrupt handler (`EI; RET`)

### 0x4000 - 0x57FF: Screen Pixel RAM (6144 bytes)
- 256x192 bitmap, ZX Spectrum standard layout
- Divided into 3 thirds of 2048 bytes each
- Each third: 8 character rows of 32 columns, 8 scanlines per row
- Address calculation: `0x4000 | ((Y & 0xC0) << 5) | ((Y & 0x38) << 2) | ((Y & 0x07) << 8) | (X & 0x1F)`

### 0x4800 - 0x4FFF: Tile Graphics (2048 bytes)
- 8x8 pixel tile definitions
- Each tile = 8 bytes (one per scanline)
- Tile 0x00 = blank, 0x16 = wall, 0x0D = floor, 0x0B = platform, etc.
- Tiles are copied from level data to screen during level setup

### 0x5000 - 0x57FF: Working Screen Buffer
- Used as a back-buffer for screen updates
- Copied to 0x4000 each frame

### 0x5800 - 0x5AFF: Attribute RAM (768 bytes)
- 32x24 color attributes
- Format: FLASH|BRIGHT|PAPER(3)|INK(3)
- 0x5800-0x581F: row 0, 0x5820-0x583F: row 1, etc.

### 0x5C00 - 0x5CFF: System Variables (partial)
- 0x5C00-0x5C3A: Standard ZX Spectrum system variables (KSTATE, LAST_K, etc.)
- 0x5C3B: CALL 0x2D00 (ROM stack check - patched or unused)
- 0x5C48: Border color storage
- 0x5C78: FRAMES counter (used for timing)

### 0x5E00 - 0x5FFF: Level Tile Buffer (512 bytes)
- Current level's 32x16 tile map loaded here
- Copied from level data area (0xB000+) during level init

### 0x6000 - 0x6FFF: Working Buffer
- Used for screen composition

### 0x7000 - 0x7FFF: Working Buffer
- Used for screen composition

### 0x8000 - 0x81FF: Guardian Definition Table (ASCII)
- ASCII-encoded guardian definitions for all 20 levels
- Format: comma-separated numbers with embedded control bytes
- Each guardian entry: sprite_number, x, y, min_x, max_x, etc.
- Control bytes: 0x16 (LD D,n), 0x09 (separator), 0x03 (command)
- 0x8000-0x806F: Level 0 (title screen) guardians
- 0x8070-0x80FF: Level 1 guardians
- 0x8100-0x81FF: Level 2+ guardians

### 0x8200 - 0x83FF: Willy Sprite Frames (512 bytes)
- 16 frames of 16x16 pixel masked sprites
- Each frame: 32 bytes (16 rows x 2 bytes)
- Frames 0-7: Willy facing right (walking animation)
- Frames 8-15: Willy facing left (walking animation)
- Format: 2 bytes per row = 16 pixels wide (masked sprite)

### 0x8400 - 0x84FF: Game Variables
| Address | Size | Description |
|---------|------|-------------|
| 0x8400 | 3 | DI; LD SP,0x9CFE; JP 0x85CC (warm-start trampoline) |
| 0x8407 | 1 | Current level number (0-19) |
| 0x8408 | 1 | Lives remaining |
| 0x8409 | 1 | (unused/flag) |
| 0x840A | 1 | (unused/flag) |
| 0x840B | 1 | (unused/flag) |
| 0x840C | 1 | (unused/flag) |
| 0x840D | 1 | (unused/flag) |
| 0x840E | 1 | (unused/flag) |
| 0x840F | 1 | (unused/flag) |
| 0x8410 | 8 | Animation frame table (4 pairs of sprite numbers) |
| 0x8418 | 16 | "AIR" + air supply digits (ASCII) |
| 0x8428 | 8 | Padding zeros |
| 0x8430 | 16 | "High Score " + 6 digits (ASCII) |
| 0x8440 | 16 | "   Score " + 6 digits + "G" (ASCII) |
| 0x8450 | 8 | "ameOver" + flags |
| 0x8457 | 1 | Current air supply (decrements each game tick) |
| 0x8458 | 1 | Music note duration counter |
| 0x8459 | 1 | Kempston joystick present flag (1=yes) |
| 0x845A | 1 | Game state: 0=playing, 0x40=title screen |
| 0x845B | 1 | Music note index |
| 0x845C | 1 | Key state bitmask (bit 0 = something pressed) |
| 0x845D | 1 | Current tune note position (0-6) |
| 0x845E-0x846F | 18 | Keyboard decode table (maps key bits to actions) |
| 0x8470-0x848F | 32 | Sprite attribute buffer (guardian sprite data) |
| 0x8490-0x84FF | 112 | Additional sprite/guardian working data |

### 0x8500 - 0x85CB: Music Data
- Note frequency and duration tables for the in-game tune ("In the Hall of the Mountain King")
- 0x8500-0x8580: Note frequency values (pairs of bytes for each note)
- 0x8580-0x85CB: Note duration and pattern data

### 0x85CC - 0x8FFF: Main Game Code
| Address | Description |
|---------|-------------|
| 0x85CC | Entry point: game initialization |
| 0x85E0 | Clear screen, copy level tiles from 0xA000 to 0x4000 |
| 0x8600 | Copy attribute data, set up level |
| 0x8620 | Kempston joystick detection (IN A,(C) with BC=0x1F) |
| 0x8640 | Title screen rendering loop |
| 0x8680 | Score display initialization |
| 0x86B0 | Clear working buffer at 0x5000 |
| 0x86C0 | Copy guardian table to working area |
| 0x8700 | Main game loop entry |
| 0x8710 | Draw air supply bar |
| 0x8730 | Draw tune notes (if enabled) |
| 0x8740 | Copy 0x5E00 to 0x5C00 (level tile buffer) |
| 0x8750 | Copy 0x7000 to 0x6000 |
| 0x8760 | Call guardian update, Willy movement, collision |
| 0x87A0 | Copy 0x6000 to 0x4000 (flip screen buffer) |
| 0x87B0 | Flash color cycling (if active) |
| 0x87D0 | Draw score display |
| 0x87E0 | Draw high score and air display |
| 0x87F0 | **Keyboard scanning routine** |
| 0x8850 | Sound/beeper output |
| 0x8870 | Kempston joystick read (IN A,(0x1F)) |
| 0x8880 | Keyboard row scanning (rows F7, EF, etc.) |
| 0x88A0 | Start game / level transition |
| 0x88B0 | Tune note decoding |
| 0x8900 | (continued) |
| 0x8A00 | Color cycling / flashing effect |
| 0x8A20 | OUT (0x58),A - write to attribute port? |
| 0x8A70 | Level completion / item collection check |
| 0x8AB0 | Copy level data from 0xA000 to 0x7000 |
| 0x8AC0 | Collision detection with guardians |
| 0x8AF0 | Sound effect output (OUT (0xFE),A with bit 4 toggling) |
| 0x8B00 | Death / collision handling |
| 0x8B60 | Jump sound effect |
| 0x8B80 | Item collection check |
| 0x8BA0 | Fall death check |
| 0x8BC0 | Collision with walls/floors |
| 0x8BE0 | Keyboard input processing for movement |
| 0x8C00 | Movement direction decoding |
| 0x8C50 | Apply movement to Willy's position |
| 0x8C80 | **Conveyor belt logic** (left direction) |
| 0x8CD0 | **Conveyor belt logic** (right direction) |
| 0x8D00 | Conveyor state reset |
| 0x8D10 | **Guardian movement AI** |
| 0x8D70 | Screen address calculation for Willy |
| 0x8DB0 | Draw guardians to screen |
| 0x8DF0 | Draw Willy sprite |
| 0x8E00 | Level completion animation |
| 0x8E70 | Draw special items (collectibles) |
| 0x8F00 | Guardian movement update |
| 0x8F60 | Special item animation/collection |
| 0x8FC0 | Check if all items collected |
| 0x8FF0 | **Sprite drawing routine** (masked sprite to screen) |

### 0x9000 - 0x93FF: Title Screen & Level Script Interpreter
| Address | Description |
|---------|-------------|
| 0x9000 | Screen address calculation helper |
| 0x9020 | Title screen animation / game over check |
| 0x9090 | Clear attributes, reset level |
| 0x90C0 | Draw title screen text |
| 0x90D0 | Border color effect |
| 0x9100 | Willy animation on title screen |
| 0x9130 | Kempston joystick read on title screen |
| 0x9200 | **Level script interpreter** |
| 0x9210 | Set channel info for sound |
| 0x9220 | Check level completion condition |
| 0x9240 | Draw Willy to screen |
| 0x9280 | Draw guardian sprites |
| 0x92C0 | Draw tile graphics |
| 0x92E0 | Guardian initialization from table |
| 0x9300 | Main game loop (called from 0x8760) |
| 0x9330 | Screen address calculation (Y,X -> HL) |
| 0x933D | Kempston joystick read |
| 0x9340 | Keyboard read for start game |
| 0x9350-0x93FF | Level script bytecode data (embedded in code area) |

### 0x9C00 - 0x9DFF: Level Script Bytecode (512 bytes)
- Custom bytecode interpreter using Z80 opcode values as commands
- 0x09 = ADD HL,BC (separator/terminator)
- 0x16 = LD D,n (load string of n bytes)
- 0x0F = RRCA (command prefix)
- 0x0D = DEC C
- 0x0A = LD A,(BC)
- 0x0B = DEC BC
- 0x0C = INC C
- 0x07 = RLCA
- 0x08 = EX AF,AF'
- 0x0E = LD C,n
- 0x10 = DJNZ
- 0x13 = INC DE
- 0x06 = LD B,n
- 0x05 = DEC B
- 0x9C00-0x9CFD: Level setup script (runs once per level)
- 0x9D00-0x9DFF: Title screen text ("MANIC MINER", "BUG-BYTE ltd. 1983", "By Matthew Smith", controls help)

### 0x9E00 - 0x9FFF: Font / Character Data
- 0x9E00-0x9E1F: Wall tile pattern (0x16)
- 0x9E20-0x9E3F: Floor tile pattern (0x17)
- 0x9E40-0x9E5F: Platform tile pattern
- 0x9E60-0x9E7F: Conveyor tile pattern (0x13)
- 0x9E80-0x9EBF: Background tile patterns
- 0x9EC0-0x9EFF: Crumbling floor tile pattern (0x38)
- 0x9F00-0x9F1F: Collectible item tile pattern (0x30)
- 0x9F20-0x9F3F: Key/door tile pattern (0x57/0x67)
- 0x9F40-0x9F9F: Additional tile patterns (0x46)
- 0x9FA0-0x9FDF: Additional tile patterns (0x45)
- 0x9FE0-0x9FFF: Blank/zero tiles

### 0xA000 - 0xAFFF: Level Tile Buffer (4096 bytes)
- Current level's tile graphics expanded to 8x8 pixel data
- Copied from level definition during init
- 0xA000-0xA3FF: First 128 tiles (8 bytes each)
- Copied to screen at 0x4000 during level setup

### 0xB000 - 0xFFFF: Level Data (20 levels)
Each level = 1024 bytes (512 tile map + 512 guardian/name data)
- 0xB000-0xB3FF: Level 0 (title screen / "The Cold Room"?)
- 0xB400-0xB7FF: Level 1 ("Central Cavern")
- 0xB800-0xBBFF: Level 2 ("The Cold Room")
- 0xBC00-0xBFFF: Level 3 ("The Menagerie")
- 0xC000-0xC3FF: Level 4 ("Abandoned Uranium Workings")
- 0xC400-0xC7FF: Level 5 ("Eugene's Lair")
- 0xC800-0xCBFF: Level 6 ("Processing Plant")
- 0xCC00-0xCFFF: Level 7 ("The Vat")
- 0xD000-0xD3FF: Level 8 ("Miner Willy meets the Kong Beast")
- 0xD400-0xD7FF: Level 9 ("Wacky Amoebatrons")
- 0xD800-0xDBFF: Level 10 ("The Endorian Forest")
- 0xDC00-0xDFFF: Level 11 ("Attack of the Mutant Telephones")
- 0xE000-0xE3FF: Level 12 ("Return of the Alien Kong Beast")
- 0xE400-0xE7FF: Level 13 ("Ore Refinery")
- 0xE800-0xEBFF: Level 14 ("Skylab Landing Bay")
- 0xEC00-0xEFFF: Level 15 ("The Bank")
- 0xF000-0xF3FF: Level 16 ("The Sixteenth Cavern")
- 0xF400-0xF7FF: Level 17 ("The Warehouse")
- 0xF800-0xFBFF: Level 18 ("Amoebatrons' Revenge")
- 0xFC00-0xFFFF: Level 19 ("Solar Power Generator")

### Level Data Format (per level, 1024 bytes):
- Bytes 0-511: 32x16 tile map (one byte per cell)
  - Tile IDs: 0x00=blank, 0x16=wall, 0x0D=floor, 0x0B=platform, 0x0E=conveyor, etc.
- Bytes 512-543: Level name (32 bytes, space-padded ASCII)
- Bytes 544-575: Guardian definitions (32 bytes)
- Bytes 576-607: Item positions (32 bytes)
- Bytes 608-639: Conveyor definitions (32 bytes)
- Bytes 640-671: Portal/door definitions (32 bytes)
- Bytes 672-703: Additional data (32 bytes)
- Bytes 704-1023: Guardian sprite graphics (5 guardians x 64 bytes each = 320 bytes)

## Key Variables Detail

### Willy State (0x8068-0x807F area)
| Address | Description |
|---------|-------------|
| 0x8068 | Willy X position (pixels) |
| 0x8069 | Conveyor movement counter |
| 0x806A | Movement direction/flags |
| 0x806B | Jump/fall state (0=ground, 1=jumping, 2=falling, 6=dead) |
| 0x806C | Willy screen address LSB |
| 0x806D | Willy screen address MSB |
| 0x806E | Air supply counter |
| 0x806F | Fall distance counter |
| 0x8070 | Animation frame pointer LSB |
| 0x8071 | Animation frame pointer MSB |
| 0x8072 | Animation frame counter |
| 0x8073 | Border/sound output value |
| 0x8074 | Item collection flag |
| 0x8075-0x807F | Item state tracking (5 items) |

### Guardian State (0x80BE-0x80FF area, 7 bytes per guardian)
| Offset | Description |
|--------|-------------|
| +0 | Guardian type/flags (bit 7 = active) |
| +1 | Y position (0-15 rows) |
| +2 | X position (0-31 columns) |
| +3 | Screen address LSB |
| +4 | Movement direction/speed |
| +5 | Min position bound |
| +6 | Max position bound |
