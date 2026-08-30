/**
 * @file media.h
 * @brief ZX-era format detection and load from an in-memory blob.
 */
#pragma once

#include "vfs.h"
#include "z80.h"
#include "ula.h"

#include <string>
#include <vector>

/** @brief Canonical format id: "tap", "z80", "scl", … or empty if unknown. */
std::string media_detect(const VfsBlob& blob);

/**
 * @brief Load a snapshot, tape, disk, cartridge, or poke file into the machine.
 *
 * May switch the ULA to 128K when the image says so.
 *
 * @retval true  CPU/memory ready to run
 * @retval false unsupported or corrupt (reason logged)
 */
bool media_load(const VfsBlob& blob, Z80& z80, ULA& ula);

/**
 * @brief Apply a .pok trainer to already-loaded RAM.
 */
bool media_apply_pok(const VfsBlob& blob, ULA& ula);

/** @brief Human-readable list of formats this build understands. */
const char* media_format_help();
