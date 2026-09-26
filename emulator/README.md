# ZXEm — details

See the [top-level README](../README.md) for the 3-step play path and the
legal notice: **no ROMs or games are shipped**.

Version **0.9**. Binary after `make release` (from the repo root or here):
`zxem` in this directory, or `emulator/zxem` from the repo root.

## Build

Needs SDL2, SDL2_image, libzip, zlib, and g++ (C++23):

```bash
make -s -j"$(nproc)"          # debug: -O0 + ASan/UBSan (slow; for bugs)
make -s test
make -s verify
make -s release               # play: -O3 -DNDEBUG, no sanitizers
```

Default `make` is **not** for playing. Use **`make release`**.

The 48K/128K/+3 ULA is **50 Hz PAL**. The run loop sleeps so one emulated
picture takes 20 ms of wall time. Window icon: `icons/zxem.svg`.

Click the window so it has keyboard focus. Number-row and keypad `0`–`9`
both map to the Spectrum keys.

## Run

```text
zxem [options] [input…]
```

No arguments prints usage (same as `-h` / `--help`). `-v` / `--version`
prints `zxem 0.9`. Options and inputs may be interleaved.

**Precedence:** CLI flags **always** win over INI (`--config` / `./config.ini`),
which wins over compiled defaults.

```bash
./zxem game.z80
./zxem games.zip --list
./zxem games.zip#folder/title.z80
./zxem games.zip --member 'title'
./zxem --model spectrum128 game.sna
./zxem --rom 48.rom game.tap
./zxem --headless --frames 100 game.tap
./zxem --pok game.pok game.z80
```

### Options

| Flag | Default | Meaning |
|------|---------|---------|
| `-h`, `--help` | | Usage |
| `-v`, `--version` | | `zxem 0.9` (never verbose) |
| `--list` | off | List playable files in a dir/zip to stdout |
| `--member NAME` | | Substring match inside a zip (prefers snapshots) |
| `--model MODEL` | spectrum48 | `spectrum48`, `spectrum128`, or `plus3` |
| `--rom FILE` | synthetic | 16K/32K/64K ROM image you own |
| `--rom-dir DIR` | `./rom` | Search for a ROM |
| `--no-system-rom` | search on | Skip `ZXEM_SYSTEM_ROM_DIR` / `./rom` auto-search |
| `--trdos-rom FILE` | off | 16K TR-DOS ROM (Beta Disk) |
| `--plus3-rom FILE` | off | 64K +3 ROM or dir of `plus3-0..3.rom` |
| `--config FILE` | `./config.ini` | INI (CLI flags always override INI) |
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
| `--keymap NAME` | spectrum | `spectrum` (1:1) or `wasd` (WASD move, Left Ctrl jump/fire) |
| `--issue 2\|3` | 3 | 48K port FE bits 5/7: Issue 3 always 1; Issue 2 = inverse of MIC |
| `--disk-out FILE` | discard | Write dirty TRD/DSK on exit |

A zip or directory without `--member` lists playable images on **stdout**.

### Formats

Loaded: `.z80` `.sna` `.szx` `.sp` `.slt` `.tap` `.tzx` `.scl` `.trd` `.rom` `.dck` `.pok` `.dsk` `.mgt` `.mdr` `.fdi` `.d80` `.d40` `.spg` (unpacked)  
Recognised but not loaded: `.ipf` (CAPS flux), `.csw` (use TAP/TZX), packed `.spg`, compressed `.udi`  
Archives: `.zip` via libzip (STORE or deflate) — **never extracted to disk**.

### Tape / disk fast-load

TAP/TZX inject CODE blocks and jump to the last CODE start, and also feed
ULA EAR (port FE bit 6) so a real 48K ROM can `LOAD ""`. SCL/TRD inject
TR-DOS CODE files and attach the disk to VG93 if `--trdos-rom` is present.
`.dsk` parses EDSK, injects PLUS3DOS CODE, and mounts sectors on a uPD765
subset (`--model plus3` / `--plus3-rom`). `.mgt` injects +D CODE/snapshots.

### 128K

`--model spectrum128` (or a 128K snapshot) enables paging on port `0x7FFD`
and AY-3-8912 on `0xFFFD`/`0xBFFD`.

### Headless batch

```bash
./zxem --headless --frames 50 game.z80
ZXEM_HEARTBEAT=1 ZXEM_GAMES=./games python3 batch_test.py
```

`ZXEM_HEARTBEAT=1` prints `Frame N, PC=… FRAMES=… scr=…` at load, every 50
frames, and at `--frames` exit (stdout, even with `--no-log`). `batch_test.py`
fails frozen runs (stuck PC without FRAMES/display-hash progress) and sanitizer hits.

## Controls

Default (`--keymap spectrum`) is a 1:1 Spectrum keyboard. There is **no in-game
rebind menu** yet. Use `--keymap wasd` or `config.ini` (`--keymap` on the CLI
wins if both are set):

```ini
[input]
keymap = wasd
```

| `--keymap wasd` | Spectrum / Kempston |
|-----------------|---------------------|
| W / Up | forward (A) + Kempston up |
| A / Left | left (Z) + Kempston left |
| D / Right | right (X) + Kempston right |
| S / Down | Kempston down |
| Left Ctrl | jump (Q) + Kempston fire |

| Always | |
|--------|--|
| arrows | Kempston (also with default keymap) |
| Esc | Quit |
| F1 | Reload |
| F5 | Save `savestate.z80` |
| F9 | Load `savestate.z80` |
| F10 | Load `savestate.z80` (alias) |

## Legal

No Sinclair or Amstrad ROM is shipped. No game files are shipped. A synthetic
ROM is generated unless you pass `--rom` with a ROM you own.
