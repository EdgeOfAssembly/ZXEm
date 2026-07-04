# Emulator Authenticity and Accuracy Assessment

## What model does the emulator emulate by default?

By default, the emulator models a **Sinclair ZX Spectrum 48K** (rubber-key or Spectrum+ case, Issue 2/3 ULA, 48KB RAM, no AY sound).

Key 48K assumptions hard-coded in the current code:

| Feature | Value | Notes |
|---------|-------|-------|
| CPU clock | 3.5 MHz | Standard 48K |
| RAM | 48KB at `0x4000-0xFFFF` | Standard 48K |
| ROM | 16KB at `0x0000-0x3FFF` | Synthetic by default; real ROM optional |
| Screen | 256×192 bitmap + 32×24 attrs | Standard Spectrum ULA |
| Colours | 15 colours + black, bright, flash | Standard ULA attribute format |
| Frame rate | 50 Hz = 69888 T-states | Standard 48K |
| T-states per line | 224 | Standard 48K |
| Active display lines | 64-255 | Standard 48K |
| Interrupt | IM1 to `0x0038` | Standard 48K ULA interrupt |
| Sound | Beeper only (bit 4 of port `0xFE`) | Standard 48K has no AY chip |

## How close to 100% authentic/accurate is it?

### CPU: ~85-90%

The Z80 core implements the full main opcode set, `CB`, `ED`, `DD`, `FD`, `DDCB`, `FDCB` prefixes, block instructions, interrupts, and the refresh register. For a self-contained game like *Manic Miner* this is likely enough, but:

- Undocumented flags and edge cases (e.g., `BIT n,(HL)` flag 3/5, `SCF`/`CCF` flag interactions, exact `DAA` behaviour) may have subtle bugs.
- `IM 0` and `IM 2` interrupt response bus values are simplified.
- Wait states and M1/M2/M3 timing are not cycle-exact.

For Manic Miner this is probably fine; for demo-scene code or copy-protected loaders it may fail.

### Memory contention: ~60%

The emulator applies a rough contention model: any memory access to `0x4000-0x7FFF` during the active display area adds 1 T-state. The real Spectrum 48K contention pattern is more complex:

- Contention varies between 0, 1, 2, 3, 4, 5, or 6 extra T-states depending on the exact T-state within the ULA's 8-T-state pixel fetch cycle.
- I/O contention has its own table.
- The "floating bus" effect is not implemented.

For Manic Miner this approximation is likely sufficient; for raster effects, border-synced demos, or copy protection it is not.

### ULA/video: ~80%

- Screen decoding is correct for the standard bitmap/attribute layout.
- Border colour and beeper bit handling are correct.
- Flash attribute toggles at ~1.6 Hz (16 frames), which is close enough.
- The screen is rendered once per frame, not line-by-line, so mid-frame effects (multicolour, attribute manipulation) are not possible.
- No ULA "snow" or other quirks.

### Audio: ~70%

The beeper is driven by `OUT (0xFE),A` bit 4 transitions, which is the correct mechanism. However:

- No low-pass filtering or tape/MIC mixing.
- The square wave is sharp; real Spectrum speaker response is softer.
- No AY-3-8912 chip (only relevant for 128K/+2/+3 models).

### I/O / keyboard: ~90%

- Keyboard matrix reading is correct.
- Kempston joystick is correct.
- No support for other joysticks (Sinclair, Cursor) yet.
- No floating bus or port contention.

### Snapshot format: ~95%

Z80 v1 48K compressed snapshots are loaded and saved correctly. v2/v3, SNA, TAP, TZX, etc. are not supported.

### ROM handling: variable

- With synthetic ROM: ~20% authentic (not a real Spectrum), but 100% legal and enough for Manic Miner.
- With a real Spectrum 48K ROM (`spec48.rom`): ~90% authentic for 48K software that uses the ROM.

## Overall estimate

| Scenario | Authenticity | Notes |
|----------|--------------|-------|
| Synthetic ROM + Manic Miner | ~75-80% | Good enough for the target game, not a real Spectrum |
| Real 48K ROM + Manic Miner | ~85-90% | Much closer; ROM calls work if any existed |
| Real 48K ROM + arbitrary 48K game | ~75-85% | Z80/ULA approximations will trip some titles |
| 128K / +2 / +3 games | 0% | Not implemented |

So with a real ROM, we are in the **mid-80s** for common 48K games. To reach true "100% authentic and accurate" we would need:

1. Exact ULA contention tables.
2. Floating-bus implementation.
3. Line-accurate / cycle-accurate rendering.
4. Verified Z80 edge cases (ideally against ZEXALL or FUSE test suite).
5. Full tape loading support (TAP/TZX) if running from cassette.
6. AY-3-8912 sound and memory paging for 128K models.

## Should the machine model be configurable?

**Yes, eventually.** Right now there is only one model worth selecting (48K), but adding a `model` config/CLI option is good future-proofing. Suggested values:

```ini
[emulator]
model = spectrum48    ; default
; model = spectrum128
; model = plus2
; model = plus2a
; model = plus3
```

For now, `model = spectrum48` is the only working choice. Implementing the others requires:

- 128K: two ROM banks, RAM paging, shadow screen, AY-3-8912 sound.
- +2: same as 128K with different ROM.
- +2A/+3: four ROM banks, disk interfaces, more paging.

I recommend adding the `model` option to the config parser now, but having it warn/exit if set to anything other than `spectrum48` until those features are implemented. That keeps the door open without pretending we support more than we do.

## Recommendation

1. Keep the default as **Spectrum 48K**.
2. Add `--model` / `model = spectrum48` option as a placeholder.
3. Focus accuracy improvements on the 48K model first:
   - Implement proper contention tables.
   - Add a ZEXALL-based test or comparison against FUSE.
   - Improve audio filtering.
4. Add 128K/+3 support only after 48K is solid.

This keeps the project honest about what it is while making it easy to grow.
