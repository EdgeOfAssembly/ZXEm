# ZX Spectrum 48K ROM Options and Accuracy Notes

## What we use today

The emulator synthesizes a minimal 16KB ROM at runtime. See `../emulator/inner_workings.md` for the exact bytes. This avoids the copyrighted Sinclair ROM and is sufficient for *Manic Miner*.

## For a more authentic experience

### Option 1: User-provided ROM (recommended, implemented)

The emulator now supports `--rom FILE` and `--rom-dir DIR`:

```bash
./zxem --rom 48.rom game.z80
./zxem --rom-dir rom game.z80
```

If a real 16KB ROM is loaded, it is mapped to `0x0000-0x3FFF`. The emulator still loads the snapshot state directly into RAM and CPU registers and starts at the snapshot PC, bypassing the ROM boot sequence. This is the correct behaviour for snapshot-based play.

Pass a 16KB 48K ROM you own; it is not stored in this repository.

### Option 2: Open-source replacement ROMs

| Project | License | Notes |
|---------|---------|-------|
| OpenSE BASIC | GPL-like | Modern replacement, not fully compatible with all games |
| Gosh Wonderful ROM | Various | Community rewrites, check license per version |
| JGHarston's ROMs | Public domain / permissive | Some clean-room ROMs exist |

None of these are **byte-identical** to the Sinclair ROM, but they can run a large fraction of games. Always verify the license before bundling.

### Option 3: 128K / +2 / +3 ROMs

For later models you need additional ROM banks and ULA features:

- 128K has two 16KB ROM banks and an AY-3-8912 sound chip.
- +2A/+3 have four ROM banks and disk/tape interfaces.
- 128K paging and AY-3-8912 are implemented. +3 needs four ROM banks (`--plus3-rom` or Fuse `plus3-0..3.rom`) plus the uPD765 subset.

## Authenticity checklist

To claim "100% authentic Spectrum ZX experience":

- [ ] Use the original Sinclair 48K/128K ROM(s) (legally sourced by the user).
- [ ] Implement exact ULA contention and floating-bus behaviour.
- [ ] Implement exact tape loading (TAP/TZX) or disk (+3) interfaces.
- [ ] For 128K: implement memory paging, shadow screen, and AY-3-8912 sound.
- [ ] Validate against a large game compatibility suite.

Our current emulator ticks none of these fully except the first if the user supplies a ROM. It is intentionally a focused, legal, minimal machine for *Manic Miner*.

## Where to find ROMs (user responsibility)

If you own a Spectrum, you can dump the ROM yourself using a simple programmer or an existing tool like `Spectaculator`, `FUSE`, or `CSpect`. Do not download ROMs unless you are certain the source has the legal right to distribute them.

## Conclusion

- **Legally clean synthetic ROM**: perfect for Manic Miner, limited elsewhere.
- **User-provided original ROM**: best legal path to full authenticity.
- **Open-source replacement ROMs**: middle ground; check licenses carefully.
- **Do not bundle or auto-download copyrighted ROMs.**
