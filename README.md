# ZXEm — ZX Spectrum 48K Emulator

A SDL2-based ZX Spectrum 48K emulator focused on accuracy, authenticity, and clean reverse engineering.

## Quick start

```bash
cd emulator
make
./zxem
```

By default `config.ini.example` is used as a template. Copy it to `config.ini` and edit paths for your system.

## Project structure

```
ZXEm/
├── emulator/          # SDL2 Spectrum 48K emulator (C++17)
├── knowledge_base/    # Long-form documentation
│   ├── emulator/      # Emulator architecture and accuracy notes
│   ├── game/          # Manic Miner reverse-engineering report
│   ├── legal/         # ROM copyright guidance
│   └── roms/          # ROM options and accuracy notes
└── reverse/           # Reverse-engineering artefacts
```

## Legal / ROM notice

This repository contains no copyrighted Sinclair ROMs and no proprietary game binaries. The emulator generates a minimal synthetic ROM at runtime. To use a real Spectrum ROM for maximum authenticity, supply your own legally owned 16KB ROM image via `--rom` or `config.ini`.

## License

The emulator code and documentation are provided for personal reverse-engineering and preservation use.
