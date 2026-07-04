# Manic Miner (1983 Bug-Byte) - I/O and Interrupts

## Interrupt Mode

- **IM 1** (Interrupt Mode 1)
- On interrupt, CPU jumps to 0x0038
- ROM at 0x0038 contains `EI; RET` (0xFB, 0xC9)
- This means interrupts simply re-enable and return - no custom ISR
- The game does NOT rely on the ULA interrupt for timing
- All timing is done via software delay loops and the FRAMES counter at 0x5C78

## ULA Screen Timing

- Standard ZX Spectrum 256x192 pixel display
- 50Hz refresh (20ms per frame)
- The game does NOT use floating bus or contended memory tricks
- Screen updates happen during the main loop, not synced to raster
- Screen flipping: back-buffer at 0x6000 copied to 0x4000 each frame
- This causes some flicker/tearing (visible in original game)

## I/O Ports Used

### OUT (0xFE), A - Border and Sound
Used extensively for:
1. **Border color**: Bits 0-2 set the border color
2. **Sound (EAR/MIC)**: Bit 4 toggled for sound output
3. **Tape output**: Bit 3 (not used by game)

Locations:
- 0x8852: Sound effect output
- 0x8928: Sound effect output
- 0x89AA: Sound effect output
- 0x8AF5: Sound effect output (jump sound)
- 0x8B70: Sound effect output (item collect)
- 0x907D: Border color set (title screen)
- 0x90D9: Border color effect
- 0x90E0: Border color effect
- 0x91B5: Sound effect
- 0x92F8: Sound effect

### IN A, (0xFE) - Keyboard Input (via IN A,(C) with B as high byte)
The ZX Spectrum keyboard is read via port 0xFE. The high byte of the address
selects which half-row to read. The game uses `IN A,(C)` with B containing
the row select byte.

**Keyboard Rows Used:**

| Row Select (B) | Keys in Row | Game Action |
|----------------|-------------|-------------|
| 0xF7FE | 1,2,3,4,5 | (not used for gameplay) |
| 0xFBFE | Q,W,E,R,T | (not used for gameplay) |
| 0xFDFE | A,S,D,F,G | Pause (A-G) |
| 0xFEFE | Shift,Z,X,C,V | (not used for gameplay) |
| 0xEFFE | 0,9,8,7,6 | Tune on/off (H-L = 6-0 row?) |
| 0xDFFE | P,O,I,U,Y | (not used for gameplay) |
| 0xBFFE | Enter,L,K,J,H | Start game (Enter), Tune (H-L) |
| 0x7FFE | Space,.,M,N,B | (not used for gameplay) |

**Actual Key Mappings (from keyboard decode table at 0x845E):**

The keyboard decode table maps key bit patterns to movement actions:
- **Left**: Q, W, E, R, T (top row) - actually Q to P = Left & Right per title screen text
- **Right**: P, O, I, U, Y (row below top)
- **Jump**: Bottom row keys (Space, ., M, N, B) - actually "Bottom row = Jump" per title screen
- **Pause**: A to G keys
- **Tune On/Off**: H to L keys
- **Start Game**: Enter key

From the title screen text at 0x9D60:
- "Q to P = Left & Right"
- "Bottom row = Jump"
- "A to G = Pause"
- "H to L = Tune On/Off"

The keyboard scanning routine at 0x87F0:
1. Reads 0xFEFE (Shift,Z,X,C,V) - checks for specific key
2. Reads 0x7FFE (Space,.,M,N,B) - jump keys
3. Reads 0xBFFE (Enter,L,K,J,H) - start/tune keys
4. Reads 0xDFFE (P,O,I,U,Y) - right movement
5. Reads 0xFBFE (Q,W,E,R,T) - left movement
6. Reads 0xF7FE (1,2,3,4,5) - additional check
7. Reads 0xEFFE (0,9,8,7,6) - tune keys

### IN A, (0x1F) - Kempston Joystick
- Standard Kempston joystick interface at port 0x1F
- Bit 0: Right
- Bit 1: Left
- Bit 2: Down
- Bit 3: Up
- Bit 4: Fire
- Detection at 0x8620: reads port 0x1F, checks if any bits respond
- If Kempston detected, sets flag at 0x8459
- Read at 0x887E and 0x933D during gameplay

### IN A, (0x80) - I/O Port (Unknown Device)
- Read at 0x8DFF, 0x8E1D, 0x8E22, 0x913C, 0x9184, 0x919E, 0x91F6
- Purpose unclear - possibly:
  - A custom interface/joystick
  - Part of the level completion animation timing
  - May be a dummy read for timing purposes
  - Could be related to the Bug-Byte version's copy protection

### OUT (0x58), A - Unknown Port
- Written at 0x8A21
- Writes a value to port 0x58
- Purpose unclear - possibly related to a custom hardware interface
- Not a standard Spectrum port

### OUT (0x5C), A - Unknown Port
- Written at 0xDA85 and 0xEA85
- These addresses are in the level data area (0xD000+ and 0xE000+)
- These are NOT actual OUT instructions in the code - they are data bytes
  that happen to decode as OUT (0x5C),A when interpreted as Z80 opcodes
- They are part of the level tile data, not executable code

## Keyboard Matrix Mapping Detail

The ZX Spectrum keyboard is an 8x5 matrix:

```
         D0   D1   D2   D3   D4
         ---  ---  ---  ---  ---
F7FE(1)  1    2    3    4    5
FBFE(2)  Q    W    E    R    T
FDFE(3)  A    S    D    F    G
FEFE(4)  SH   Z    X    C    V
EFFE(5)  0    9    8    7    6
DFFE(6)  P    O    I    U    Y
BFFE(7)  ENT  L    K    J    H
7FFE(8)  SP   .    M    N    B
```

A key is pressed when its corresponding bit is 0 in the input byte.
The game reads each row and checks which bits are clear.

## Sound Generation

Sound is produced by toggling bit 4 of the value written to OUT (0xFE).
This toggles the EAR/MIC output which drives the internal speaker.

Sound routine at 0x8AF0:
```
  LD C,0x20        ; pitch delay
  LD A,(0x8073)    ; current output value
  OUT (0xFE),A     ; write to port
  XOR 0x18         ; toggle bit 4 (and bits 3,4)
  LD B,D           ; duration counter
.loop:
  DJNZ .loop       ; delay loop
  DEC C
  JR NZ,outer
```

The music system uses a note table at 0x8500 with frequency/duration pairs.
The tune plays "In the Hall of the Mountain King" during gameplay.
