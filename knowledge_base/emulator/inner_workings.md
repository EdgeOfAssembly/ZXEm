# Manic Miner SDL2 ZX Spectrum Emulator — Inner Workings

## Overview

ZXEm is a ZX Spectrum 48K/128K/+3 machine using SDL2 for video, audio, keyboard, and joystick, written in C++23.

## Architecture

```
+-----------+     +-----------------+     +------------------+
|  main.cpp |---->|  Z80 CPU core   |<--->|  ULA (memory/I/O)|
| SDL2 loop |     |  (z80.cpp/h)    |     |  (ula.cpp/h)     |
+-----------+     +-----------------+     +---------+--------+
     |                                           |
     v                                           v
+-----------+                             +-------------+
| snapshot  |                             |  keyboard   |
| loader    |                             |  Kempston   |
| (z80 fmt) |                             |  beeper     |
+-----------+                             +-------------+
```

### Source files

| File | Responsibility |
|------|----------------|
| `main.cpp` | SDL2 window, event loop, audio callback, keyboard/joystick mapping, save-state UI, 50Hz frame timing |
| `z80.cpp/h` | Full-enough Z80 instruction decoder, flags, interrupts, M1 `R` increment, contention callback |
| `ula.cpp/h` | Synthetic ROM, 48KB RAM, memory read/write, I/O ports (keyboard matrix, Kempston, ULA/beeper), screen decode, contention model |
| `snapshot.cpp/h` | Load and save `.z80` v1 snapshots |
| `config.cpp/h` | INI-style configuration loader |
| `Makefile` | Build with `sdl2-config` |
| `config.ini.example` | Example configuration file |

## Z80 CPU core

### Registers

All standard 8-bit and 16-bit registers are present, plus index registers and interrupt state:

```cpp
A, F, B, C, D, E, H, L          // main set
A_, F_, B_, C_, D_, E_, H_, L_  // alternate set
IX, IY, SP, PC                  // 16-bit pointers
I, R                            // interrupt vector, refresh
IFF1, IFF2, IM                  // interrupt flip-flops and mode
halted, ei_pending, nmi_pending // execution state
```

### Flags

The `F` register uses the standard Z80 layout:

| Bit | Flag | Meaning |
|-----|------|---------|
| 7 | S | Sign (copy of bit 7 of result) |
| 6 | Z | Zero |
| 5 | 5 | Undocumented, copy of bit 5 |
| 4 | H | Half-carry |
| 3 | 3 | Undocumented, copy of bit 3 |
| 2 | P/V | Parity/overflow |
| 1 | N | Subtract |
| 0 | C | Carry |

Parity is computed for logical operations; overflow is computed for arithmetic. `DAA`, `SCF`, `CCF`, and the undocumented flag bits are handled.

### Instruction set coverage

The core implements:

- All main opcodes (`0x00-0xFF`) including 8/16-bit load, ALU, INC/DEC, rotates, `DAA`, `CPL`, `HALT`, etc.
- `CB` prefix: bit/rotate/shift instructions on registers and `(HL)`.
- `ED` prefix: block instructions, 16-bit ADC/SBC, port instructions, `NEG`, `RETN`, `RETI`, `IM`, `R`, `I`.
- `DD`/`FD` prefixes: `IX`/`IY` indexed operations including the undocumented `DDCB`/`FDCB` shifts.
- Interrupts: `IM 0/1/2`, `EI`/`DI` (with one-instruction delay for `EI`), `NMI`, and maskable `INT`.
- Refresh register `R` is incremented on every M1 fetch; bit 7 is preserved.

### Memory contention

The CPU calls `ula->isContended(addr, tstate)` after each memory access. If the address is in the ULA range (`0x4000-0x7FFF`) and the current `tstate` falls within the active display area, the instruction cost is increased by one T-state. This matches the rough Spectrum 48K contention model and keeps interrupt timing close to real hardware.

## ULA and memory model

### ROM

Because the original Sinclair ZX Spectrum 48K ROM is copyrighted, the emulator synthesizes a minimal compatible ROM at runtime:

| Address | Bytes | Purpose |
|---------|-------|---------|
| `0x0000` | `F3 C3 03 93` | `DI ; JP 0x9303` (reset vector jumps to snapshot PC) |
| `0x0038` | `FB C9` | `EI ; RET` (IM1 interrupt handler) |
| all other | `00` | NOPs / harmless |

This is sufficient for *Manic Miner* because the game is self-contained in RAM and makes no ROM calls. It is **not** sufficient for arbitrary Spectrum software that relies on the real ROM.

### RAM

48KB of RAM occupies `0x4000-0xFFFF`. It is loaded from the `.z80` snapshot decompressor or from a raw `ram48.bin` file. Screen memory starts at `0x4000`, attribute memory at `0x5800`.

### I/O ports

#### `IN A,(0xFE)` — keyboard matrix

The Spectrum keyboard is a 8×5 matrix. The high byte of the I/O address selects which half-row(s) to read. Bits 0-4 of the returned byte are the five keys in that row; a bit is **0** when the key is pressed.

The emulator stores the matrix in `keyboard[8]` and `AND`s together every row whose select bit is clear.

| Row select byte | Port (full 16-bit) | Keys (D0-D4) |
|-----------------|--------------------|--------------|
| `0xFE` bit 0 clear → row 0 | `0xFEFE` | Shift, Z, X, C, V |
| bit 1 clear → row 1 | `0xFDFE` | A, S, D, F, G |
| bit 2 clear → row 2 | `0xFBFE` | Q, W, E, R, T |
| bit 3 clear → row 3 | `0xF7FE` | 1, 2, 3, 4, 5 |
| bit 4 clear → row 4 | `0xEFFE` | 0, 9, 8, 7, 6 |
| bit 5 clear → row 5 | `0xDFFE` | P, O, I, U, Y |
| bit 6 clear → row 6 | `0xBFFE` | Enter, L, K, J, H |
| bit 7 clear → row 7 | `0x7FFE` | Space, ., M, N, B |

#### `IN A,(0x1F)` — Kempston joystick

Returns an active-high byte:

| Bit | Direction |
|-----|-----------|
| 0 | Right |
| 1 | Left |
| 2 | Down |
| 3 | Up |
| 4 | Fire |

#### `OUT (0xFE),A` — border and beeper

- Bits 0-2 set the border colour.
- Bit 4 toggles the EAR/MIC output, which the emulator uses as the beeper square wave.

## Audio

The emulator uses an SDL2 audio callback with a small lock-free ring buffer. Every time the game writes to port `0xFE` and changes bit 4, a new sample (`+5000` or `-5000`) is pushed into the buffer. The audio callback reads those samples and produces a square wave matching the game's own timing.

This is a simple representation of the Spectrum's single-bit beeper. It does not currently filter or mix tape audio.

## Video

The screen is rendered from:

- Bitmap RAM at `0x4000-0x57FF` (256×192 pixels in the standard Spectrum interleaved format).
- Attribute RAM at `0x5800-0x5AFF` (32×24 cells, 8 bytes per cell).

Attribute format:

| Bits | Meaning |
|------|---------|
| 7 | FLASH |
| 6 | BRIGHT |
| 5-3 | PAPER colour |
| 2-0 | INK colour |

The 16-colour palette uses the standard Spectrum colours. The window is scaled 3× from 256×192 to 768×576.

## Frame timing

- Z80 clock: 3.5 MHz
- 50 Hz frames → 69888 T-states per frame.
- The ULA tracks `frame_tstates`. When it reaches `69888`, an IM1 interrupt is delivered if `IFF1` is set, and the counter wraps.
- `SDL_Delay` is used to throttle to ~20 ms per frame.

## Snapshot support

### Loading

`loadZ80()` reads the 30-byte v1 header and decompresses the 48KB RAM block using the classic v1 RLE format:

- Literal bytes are emitted directly.
- `ED ED nn vv` emits `nn` copies of `vv` (or 256 if `nn == 0`).
- `ED ED 00 ED` is the terminator and represents one literal `0xED` byte.
- A lone literal `0xED` must be followed by another `0xED` to distinguish it from an RLE marker; the decompressor strips the duplicate.

### Saving

`saveZ80()` writes a valid v1 snapshot from the current CPU and RAM state. This is used for quick save-states (`F5`/`F9`/`F10`). The compressor uses RLE for runs of 4 or more identical bytes and escapes literal `0xED` bytes.

## Controls

See `README.md` for the full keyboard map. Special emulator keys:

| Key | Action |
|-----|--------|
| `F1` | Reset to initial snapshot state |
| `F5` | Save state to `savestate.z80` |
| `F9` | Quick save state |
| `F10` | Quick load state |
| `Esc` | Quit |

SDL joystick support is enabled. Axis 0 maps to Kempston left/right, and common buttons map to Jump/Start.

## Known limitations

1. The synthetic ROM is not a real Spectrum ROM. Games that call ROM routines (e.g., `BEEP`, `CLS`, floating-point calculator) will crash or behave incorrectly.
2. Floating-bus and high-fidelity ULA contention are approximated.
3. Audio is a raw square wave without low-pass filtering or AY-3-8912 support (the 48K model has no AY chip anyway).
4. Only `.z80` v1 48K snapshots are supported; v2/v3, SNA, TAP, TZX, etc. are not.
5. No built-in debugger, disassembler, or tape loading UI yet.

## Configuration file

The emulator loads an INI-style `config.ini` by default, with sections `[emulator]`, `[rom]`, `[input]`, and `[video]`. Example:

```ini
[emulator]
; game = game.z80

[rom]
; file = 48.rom
rom_dir = rom
```

Command-line options (`--rom`, `--rom-dir`, positional game path, `--config`) override the config file. This lets the user set their normal defaults once while still running one-off games easily.

## Extending the emulator

To add a real Spectrum ROM:

1. Obtain a legally licensed 16KB Spectrum 48K ROM image.
2. Load it into `ula.rom[0x0000-0x3FFF]` at startup instead of synthesizing.
3. Change the reset vector to `0x0000` and remove the `JP 0x9303`.
4. Load snapshots via the ROM's own handling (or keep direct `.z80` loading).

See `legal/rom_legal.md` for copyright guidance.
