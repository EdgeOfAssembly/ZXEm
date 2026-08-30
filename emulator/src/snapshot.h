/**
 * @file snapshot.h
 * @brief In-memory loaders for .z80 / .sna / .tap / .tzx plus ROM helpers.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include "z80.h"
#include "ula.h"

bool load_z80(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool load_sna(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool load_tap(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool load_tzx(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool load_rom_blob(const uint8_t* data, size_t size, ULA& ula);
bool load_rom_file(const char* path, ULA& ula);
bool load_rom_from_dir(const char* dir, ULA& ula);
bool save_z80(const char* path, const Z80& z80, const ULA& ula);

/** @brief Z80 v2/v3 page number (3–10) → 16K RAM bank (0–7), or -1. */
int z80_page_to_bank(uint8_t page);
