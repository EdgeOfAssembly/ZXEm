/**
 * @file tape.h
 * @brief Virtual cassette: TAP/TZX pulses driving ULA EAR (port FE bit 6).
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

/**
 * @brief Square-wave tape deck advanced in ULA T-states.
 *
 * Pilot/sync/bit timings match the 48K ROM loader. Fast-inject of CODE
 * blocks still happens in the TAP/TZX parsers; this path is for BASIC
 * `LOAD ""` when a real ROM is mapped.
 */
class TapeDeck
{
public:
    TapeDeck();

    /** @brief Clear pulses and stop the motor. */
    void reset();

    /**
     * @brief Build a standard-speed pulse stream from a TAP image.
     * @param[in] data TAP bytes.
     * @param[in] size Length in bytes.
     * @return true if at least one block was encoded.
     */
    bool load_tap(const uint8_t* data, size_t size);

    /**
     * @brief Encode TZX data blocks 0x10 / 0x11 / 0x14 as standard pulses.
     * @param[in] data TZX bytes.
     * @param[in] size Length in bytes.
     * @return true if at least one data block was encoded.
     */
    bool load_tzx(const uint8_t* data, size_t size);

    /** @brief Advance the edge schedule by @p cycles T-states. */
    void step(int cycles);

    /**
     * @brief Current EAR level.
     * @return true if bit 6 of port FE should be set (high).
     */
    bool ear_high() const { return level_; }

    /** @brief True when a pulse stream is loaded (no per-call vector check). */
    bool loaded() const { return loaded_; }

private:
    void add_edge(uint32_t tstates);
    void add_pilot(int pulses, uint32_t period);
    void add_block(uint8_t flag, const uint8_t* payload, uint16_t payload_len, uint8_t checksum);

    std::vector<uint32_t> edges_;
    size_t idx_;
    uint32_t remain_;
    bool level_;
    bool loaded_;
};
