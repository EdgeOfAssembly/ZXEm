# ZXEm — ZX Spectrum 48K/128K Emulator

SDL2 emulator aimed at the full World of Spectrum-style collection
(`/mnt/games` and `/mnt/Games.zip`) without extracting archives.

```bash
cd emulator
make -s -j"$(nproc)"
./zxem -h
./zxem /mnt/Games.zip --list | head
./zxem /mnt/Games.zip --member 'Manic Miner'
make -s test
make -s verify
```

See `emulator/README.md` for CLI, formats, and controls.
Format notes: `re/formats/`.
