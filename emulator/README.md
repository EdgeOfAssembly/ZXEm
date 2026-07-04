# ZXEm — ZX Spectrum 48K Emulator

A SDL2-based ZX Spectrum 48K emulator focused on accuracy, authenticity, and clean reverse engineering.

## Build

Requires SDL2 and a C++17 compiler. On Gentoo:

```bash
sudo emerge -av libsdl2
```

Then build:

```bash
make
```

## Run

```bash
./zxem
```

By default `config.ini.example` is used as a template; copy it to `config.ini` and edit paths for your system.

You can also load a different `.z80` snapshot or a real Spectrum ROM:

```bash
./zxem /path/to/game.z80
./zxem --rom /path/to/48.rom
./zxem --rom-dir rom /path/to/game.z80
./zxem --config /path/to/config.ini
```

### Configuration file (`config.ini`)

For day-to-day use, create `config.ini` next to the `zxem` binary:

```ini
[emulator]
game = /path/to/game.z80

[rom]
; Uncomment the next line to use a real Spectrum ROM:
; file = /path/to/spec48.rom
rom_dir = rom
```

Settings are loaded from `config.ini` by default. Command-line arguments override the config file, so you can still run one-off games without editing the file.

If no ROM is supplied, the emulator creates a minimal synthetic ROM automatically. This synthetic ROM contains no copyrighted Sinclair code and is sufficient for self-contained 48K games like *Manic Miner*.

## Controls

### Keyboard (Spectrum layout)

| PC key | Spectrum key(s) | Action |
|--------|-----------------|--------|
| `←` arrow | Kempston left / `Q` / `A` | Move left |
| `→` arrow | Kempston right / `E` / `D` / `O` / `P` | Move right |
| `Space` | `Space` / `M` / `N` / `B` | Jump |
| `Enter` | `Enter` | Start game |
| `P` | `P` | Pause |
| `H` / `J` / `K` / `L` | `H` / `J` / `K` / `L` | Toggle tune |
| `Esc` | — | Quit |

### Emulator hotkeys

| Key | Action |
|-----|--------|
| `F1` | Reset to initial snapshot state |
| `F5` | Save state to `savestate.z80` |
| `F9` | Quick save state |
| `F10` | Quick load state |

### Joystick / gamepad

SDL joystick support is enabled automatically if a joystick is present. Currently mapped:

- D-pad / left stick X axis → Kempston left/right
- Button 0 (A/Cross) → Jump
- Button 1 (B/Circle) → Start
- Button 6 (Select) → Jump
- Button 7 (Start) → Start

This will be refined once a real USB joystick is tested.

## How it works

- The 30-byte Z80 v1 snapshot header gives the initial CPU state.
- The 48KB RAM block is decompressed from the `.z80` file using the classic v1 RLE format.
- A synthetic 16KB ROM at `0x0000-0x3FFF` provides the IM1 ISR at `0x0038` (`EI; RET`) and a reset vector; the game is entirely self-contained in RAM and makes no ROM calls.
- The Z80 core executes instructions, applies Spectrum 48K memory contention for `0x4000-0x7FFF`, and handles IM1 interrupts.
- The ULA decodes the `0x4000` pixel bitmap and `0x5800` attributes into a 256×192 texture, scaled 3× to a 768×576 window.
- Sound is generated from the EAR/MIC bit (bit 4) of `OUT (0xFE),A` writes.

## File layout

```
emulator/
├── Makefile
├── README.md
├── zxem                    # compiled binary
├── src/
│   ├── main.cpp            # SDL2 loop, input, audio, save states
│   ├── z80.cpp/h           # Z80 CPU emulation
│   ├── ula.cpp/h           # memory, I/O, screen, contention
│   ├── snapshot.cpp/h      # .z80 v1 load/save
│   └── config.cpp/h        # INI-style config loader
├── config.ini.example      # example configuration file
└── obj/                    # build artifacts
```

## Reverse-engineering notes

See `../reverse/` for the full reverse-engineering report on *Manic Miner*.

See `../knowledge_base/` for long-form documentation on the emulator, the game, ROM legal issues, and ROM accuracy options.

## Legal / ROM notice

This emulator does not include the copyrighted Sinclair ZX Spectrum ROM. A minimal synthetic ROM is generated at runtime. The original game snapshot is the property of its respective copyright holders and is used here for personal reverse-engineering.

If you want full Spectrum compatibility, you can supply your own legally owned 16KB Spectrum 48K ROM image. See `../knowledge_base/legal/rom_legal.md`.
