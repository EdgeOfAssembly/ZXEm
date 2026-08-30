# ZXEm — ZX Spectrum Emulator

SDL2 ZX Spectrum 48K/128K emulator. Loads the World of Spectrum-style
collection from the filesystem **or from a zip archive in-place** (no extract).

Version **0.2**.

## Build

Requires SDL2, libzip, zlib, and g++ (C++23):

```bash
make -s -j"$(nproc)"
make -s test
make -s verify
```

## Run

```text
zxem [options] [input…]
```

No arguments prints usage (same as `-h` / `--help`). `-v` / `--version`
prints `zxem 0.2`. Options and inputs may be interleaved.

```bash
./zxem /path/to/game.z80
./zxem /mnt/Games.zip --list
./zxem /mnt/Games.zip#Games/Manic\ Miner/Manic\ Miner\ (1983)(Bug-Byte).z80
./zxem /mnt/Games.zip --member 'Manic Miner'
./zxem --model spectrum128 game.sna
./zxem --headless --frames 100 game.tap
./zxem --pok game.pok game.z80
```

### Options

| Flag | Default | Meaning |
|------|---------|---------|
| `-h`, `--help` | | Usage |
| `-v`, `--version` | | `zxem 0.2` (never verbose) |
| `--list` | off | List playable files in a dir/zip to stdout |
| `--member NAME` | | Substring match inside a zip (prefers snapshots) |
| `--model MODEL` | spectrum48 | `spectrum48` or `spectrum128` |
| `--rom FILE` | synthetic | 16K/32K ROM image |
| `--rom-dir DIR` | `./rom` | Search for a ROM |
| `--config FILE` | `./config.ini` | INI overrides |
| `--pok FILE` | | Apply POK cheats after load |
| `--headless` | off | No SDL window |
| `--frames N` | | Run N frames and exit (implies `--headless`) |
| `--no-audio` | audio on | Disable audio |
| `--no-log` | log on | Disable RE logging |
| `--log-file PATH` | stderr | Also write the RE log |
| `--log-level LVL` | info | error\|warn\|info\|debug\|trace |
| `--trace-cpu` | off | Every instruction (slow) |
| `--trace-io` | off | Port I/O |
| `--verbose` | | `--log-level debug` |

A zip or directory without `--member` lists playable images on **stdout**.

### Formats

Loaded: `.z80` `.sna` `.szx` `.sp` `.slt` `.tap` `.tzx` `.scl` `.trd` `.rom` `.dck` `.pok`  
Recognised (needs more hardware): `.dsk` `.mgt` `.mdr` `.fdi` `.udi` `.csw` `.d80` `.d40` `.ipf` `.spg`  
Archives: `.zip` via libzip (STORE or deflate) — **never extracted to disk**.

`/mnt/Games.zip` is the 1.7 GiB STORE archive of `/mnt/games`.

### Tape / disk fast-load

TAP/TZX inject CODE blocks and jump to the last CODE start. SCL/TRD inject
TR-DOS CODE files. BASIC-only loaders still need a real Spectrum ROM.

### 128K

`--model spectrum128` (or a 128K snapshot) enables paging on port `0x7FFD`
and AY-3-8912 on `0xFFFD`/`0xBFFD`.

### Headless batch

```bash
./zxem --headless --frames 50 /mnt/games/Manic\ Miner/*.z80
python3 batch_test.py
```

## Controls

| PC key | Spectrum |
|--------|----------|
| arrows | Kempston |
| Space, Enter, letters | matching Spectrum keys |
| Esc | Quit |
| F1 | Reload |
| F5 / F9 | Save `savestate.z80` |
| F10 | Load `savestate.z80` |

## Legal

No Sinclair ROM is shipped. A synthetic ROM is generated unless you pass
`--rom` with a ROM you own. See `../knowledge_base/legal/`.
