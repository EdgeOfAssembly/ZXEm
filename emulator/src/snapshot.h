#pragma once
#include <cstdint>
#include "z80.h"
#include "ula.h"

bool loadZ80(const char* path, Z80& z80, ULA& ula);
bool saveZ80(const char* path, const Z80& z80, const ULA& ula);
bool loadROM(const char* path, ULA& ula);
bool loadROMFromDir(const char* dir, ULA& ula);
