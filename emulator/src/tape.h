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
 * TAP and TZX 0x10 use 48K ROM loader timings. TZX 0x11/0x14 use the
 * block's own pulse lengths. Fast-inject of CODE still happens in the
 * TAP/TZX parsers; this path is for BASIC `LOAD ""` with a real ROM.
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
     * @brief Encode TZX 0x10 at ROM speed; 0x11/0x14 at the block's own timings.
     * @param[in] data TZX bytes.
     * @param[in] size Length in bytes.
     * @return true if at least one data block was encoded.
     * @note 0x11 uses the 18-byte turbo header (pilot/sync/zero/one, used-bits).
     *       0x14 uses its zero/one pulse lengths. Checksum is the file byte, not 0.
     */
    bool load_tzx(const uint8_t* data, size_t size);

    /** @brief Advance the edge schedule by @p cycles T-states. */
    void step(int cycles);

    /**
     * @brief Number of scheduled EAR edges.
     * @return Count of pulse widths in the edge list.
     */
    size_t pulse_count() const { return edges_.size(); }

    /**
     * @brief Width of one scheduled EAR edge.
     * @param[in] i Edge index.
     * @return T-states, or 0 if @p i is out of range.
     */
    uint32_t pulse_at(size_t i) const
    {
        return (i < edges_.size()) ? edges_[i] : 0;
    }

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
    void add_byte_bits(uint8_t b, int nbits, uint32_t zero, uint32_t one);
    void add_data(const uint8_t* data, uint32_t n, uint32_t zero, uint32_t one, uint8_t used_bits);
    void add_block(uint8_t flag, const uint8_t* payload, uint16_t payload_len, uint8_t checksum);

    std::vector<uint32_t> edges_;
    size_t idx_;
    uint32_t remain_;
    bool level_;
    bool loaded_;
};
