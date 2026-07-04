# ROM Legality and Accuracy Assessment

## Legal situation

### The original Sinclair ROM

The Sinclair ZX Spectrum 48K/128K ROMs are copyrighted software. Amstrad (now part of Sky) owns the copyright to the 48K Spectrum ROM and has historically granted permission for **non-commercial** use in emulators, but this permission does **not** make the ROM public domain or freely redistributable for any purpose. The safest approach is to treat a real Spectrum ROM binary the same way you would treat any other copyrighted game ROM:

- Do not redistribute it.
- Do not bundle it with your emulator.
- Only use a ROM image that you yourself have dumped from hardware you own.

### Our synthetic ROM

The emulator in `/tmp/reverse/emulator/` does **not** contain or use a real Spectrum ROM. It generates a minimal synthetic 16KB ROM at runtime with only:

- A reset vector: `DI ; JP 0x9303`
- An IM1 interrupt handler: `EI ; RET`
- All other bytes zero.

This synthetic ROM contains **no copyrighted Sinclair code** and is therefore safe to distribute and run. However, it is **not** a real Spectrum ROM, so it only works for games (like this version of *Manic Miner*) that are completely self-contained in RAM and make no ROM calls.

## Accuracy trade-offs

### What the synthetic ROM cannot do

A real Spectrum ROM provides hundreds of routines that games may call:

| ROM routine | Typical address | Purpose |
|-------------|---------------|---------|
| `BEEP` | `0x03B5` | Play a note through the beeper |
| `CLS` | `0x0DAF` | Clear the screen |
| `PR-ALL` | `0x09F4` | Print a character |
| `KEY-SCAN` | `0x028E` | Read the keyboard |
| Floating-point calculator | `0x2D28` | Arithmetic and string handling |
| Tape loader/saver | `0x0556` / `0x0970` | Load and save programs from cassette |

Our synthetic ROM has none of these. Any game that `CALL`s or `JP`s into the ROM will almost certainly crash or hang. This means:

- **Works**: *Manic Miner (Bug-Byte, 1983)* because it is entirely self-contained.
- **May fail**: Many other Spectrum games, especially those using `BEEP`, `CLS`, ROM printer routines, or tape loading.
- **Will fail**: Games that rely on the full Spectrum ROM or 128K / +2 / +3 features (AY-3-8912 sound, second RAM bank, etc.).

## Can we make the ROM 100% accurate and authentic?

Yes, but only by using a **real copyrighted ROM dump**. There is no open-source or public-domain implementation that is byte-for-byte identical to the original Sinclair ROM, because the original is still under copyright. Some projects provide **reimplemented ROMs** (clean-room implementations), but those are not the authentic Sinclair ROM and may have subtle compatibility differences.

### Options, from safest to most authentic

1. **Keep the synthetic ROM** (current approach)
   - Legally clean.
   - Limited compatibility.
   - Perfect for Manic Miner.

2. **User-supplied ROM**
   - Allow the emulator to load a ROM file from disk at runtime.
   - The user must provide their own legally owned ROM dump.
   - Legally clean for us; responsibility falls on the user.
   - Enables near-perfect compatibility with most Spectrum software.

3. **Download common ROMs temporarily for testing**
   - **Not recommended.** Downloading copyrighted ROMs from the internet, even temporarily, is copyright infringement in most jurisdictions unless you already own the hardware and the source is your own dump.
   - There are well-known archives (e.g., World of Spectrum, spectrum4ever) that host ROMs, but these are generally not licensed for redistribution.
   - We should **not** download, store, or redistribute them automatically.

4. **Use an open-source reimplementation**
   - Projects like `ROM0` (the OpenSE BASIC replacement) or some community ROMs exist.
   - These are legal to distribute if their license permits it.
   - They are **not** 100% authentic, but they can be very close for many games.

## Practical recommendation

The emulator now supports automatic ROM handling:

```bash
./zxem                          # synthetic ROM, default game
./zxem game.z80                 # synthetic ROM, load game.z80
./zxem --rom /path/to/48.rom    # user-supplied ROM
./zxem --rom-dir rom game.z80   # auto-detect ROM in ./rom
./zxem --config config.ini       # load settings from config file
```

Behaviour:
1. If `--rom FILE` is given, that 16KB file is loaded into `0x0000-0x3FFF`.
2. Otherwise the emulator scans `--rom-dir` (default `./rom`) for known filenames (`48.rom`, `spectrum.rom`, etc.) or any 16384-byte file.
3. Settings from `config.ini` (if present) are loaded first and can be overridden by CLI options.
4. If no ROM is found, a legally clean synthetic ROM is generated at runtime.

This gives users the option of full Spectrum compatibility without putting any copyrighted code in our repository.

## User-supplied ROM test

A real Spectrum 48K ROM (`spec48.rom`, 16KB, copyright Amstrad, used with permission from the Spectrum For Everyone collection) was tested with:

```bash
./zxem --rom /tmp/zx-roms/spectrum16-48/spec48.rom \
                 /tmp/reverse/Manic_Miner_1983_Bug_Byte_Software.z80
```

The emulator loaded the ROM successfully and the game ran. This confirms the `--rom` path works for maximum authenticity. The ROM file itself is **not** copied into the emulator repository; it is only loaded from an external path at runtime.

## Summary

- **Yes**, we can create a synthetic ROM dynamically and stay legally clean.
- **No**, we should not download common Spectrum ROMs automatically, even temporarily, because they are copyrighted.
- **Yes**, we can let the user supply their own ROM for maximum accuracy.
- The current synthetic ROM is correct and sufficient for *Manic Miner* because the game does not use the Spectrum ROM at all.
- A real Spectrum 48K ROM can be loaded with `--rom` for full authenticity when the user has a legal copy.
