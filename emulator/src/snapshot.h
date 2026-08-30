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
bool load_plus3_roms_from_dir(const char* dir, ULA& ula);
bool load_trdos_rom_file(const char* path, ULA& ula);
bool load_system_roms(ULA& ula, const char* model);
bool save_z80(const char* path, const Z80& z80, const ULA& ula);

/**
 * @brief Z80 snapshot page → 16K RAM bank, or -1.
 *
 * 128K: file pages 3–10 are banks 0–7.
 * 48K: pages 8/4/5 are 4000/8000/C000 (banks 5/2/0).
 */
int z80_page_to_bank(uint8_t page, bool is128k = true);
