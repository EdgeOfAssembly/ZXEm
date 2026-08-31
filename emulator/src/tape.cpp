/**
 * @file tape.cpp
 * @brief TAP/TZX pulse encoder for the ULA EAR bit.
 */

#include "tape.h"

#include "cursor.h"
#include "log.h"

#include <cstring>

namespace
{
constexpr uint32_t kPilot = 2168;
constexpr uint32_t kSync1 = 667;
constexpr uint32_t kSync2 = 735;
constexpr uint32_t kBit0 = 855;
constexpr uint32_t kBit1 = 1710;
constexpr uint32_t kPause = 3500000; /* ~1 s at 3.5 MHz */
}

TapeDeck::TapeDeck()
{
    reset();
}

void TapeDeck::reset()
{
    edges_.clear();
    idx_ = 0;
    remain_ = 0;
    level_ = true;
    loaded_ = false;
}

void TapeDeck::add_edge(uint32_t tstates)
{
    if (tstates == 0)
    {
        return;
    }
    edges_.push_back(tstates);
}

void TapeDeck::add_pilot(int pulses, uint32_t period)
{
    for (int i = 0; i < pulses; i++)
    {
        add_edge(period);
    }
}

void TapeDeck::add_byte_bits(uint8_t b, int nbits, uint32_t zero, uint32_t one)
{
    if (nbits <= 0)
    {
        return;
    }
    if (nbits > 8)
    {
        nbits = 8;
    }
    for (int i = 7; i >= 8 - nbits; i--)
    {
        const uint32_t p = (b & (1u << i)) ? one : zero;
        add_edge(p);
        add_edge(p);
    }
}

void TapeDeck::add_data(const uint8_t* data, uint32_t n, uint32_t zero, uint32_t one, uint8_t used_bits)
{
    if (data == nullptr || n == 0)
    {
        return;
    }
    uint8_t last_bits = used_bits;
    if (last_bits == 0 || last_bits > 8)
    {
        last_bits = 8;
    }
    for (uint32_t i = 0; i < n; i++)
    {
        const int bits = (i + 1u == n) ? static_cast<int>(last_bits) : 8;
        add_byte_bits(data[i], bits, zero, one);
    }
}

void TapeDeck::add_block(uint8_t flag, const uint8_t* payload, uint16_t payload_len, uint8_t checksum)
{
    const int pilot = (flag == 0x00) ? 8063 : 3223;
    add_pilot(pilot, kPilot);
    add_edge(kSync1);
    add_edge(kSync2);
    add_byte_bits(flag, 8, kBit0, kBit1);
    add_data(payload, payload_len, kBit0, kBit1, 8);
    add_byte_bits(checksum, 8, kBit0, kBit1);
    add_edge(kPause);
}

/**
 * @brief Skip a TZX block that is not EAR-encoded (same IDs as snapshot.cpp).
 * @param[in,out] c Cursor just after the block id.
 * @param[in] id TZX block identifier.
 * @return true if the block was skipped; false if truncated or length unknown.
 *
 * Unknown ids with a documented length (0x15, 0x16–0x19 DWORD, 0x34, 0x40)
 * are skipped; anything else stops the parse.
 */
static bool tzx_skip_id(ByteCursor& c, uint8_t id)
{
    switch (id)
    {
        case 0x12:
            return c.skip(4);
        case 0x13:
        {
            uint8_t n = 0;
            return c.get8(n) && c.skip(static_cast<size_t>(n) * 2u);
        }
        case 0x15:
        {
            if (!c.skip(5))
            {
                return false;
            }
            uint32_t n = 0;
            return c.get24le(n) && c.skip(n);
        }
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        {
            uint32_t n = 0;
            return c.get32le(n) && c.skip(n);
        }
        case 0x20:
        case 0x23:
        case 0x24:
            return c.skip(2);
        case 0x21:
        {
            uint8_t n = 0;
            return c.get8(n) && c.skip(n);
        }
        case 0x22:
        case 0x25:
        case 0x27:
            return true;
        case 0x26:
        {
            uint16_t n = 0;
            return c.get16le(n) && c.skip(static_cast<size_t>(n) * 2u);
        }
        case 0x28:
        {
            uint16_t n = 0;
            return c.get16le(n) && c.skip(n);
        }
        case 0x2A:
            return c.skip(4);
        case 0x2B:
            return c.skip(5);
        case 0x30:
        {
            uint8_t n = 0;
            return c.get8(n) && c.skip(n);
        }
        case 0x31:
        {
            uint8_t t = 0;
            uint8_t n = 0;
            return c.get8(t) && c.get8(n) && c.skip(n);
        }
        case 0x32:
        {
            uint16_t n = 0;
            return c.get16le(n) && c.skip(n);
        }
        case 0x33:
        {
            uint8_t n = 0;
            return c.get8(n) && c.skip(static_cast<size_t>(n) * 3u);
        }
        case 0x34:
            return c.skip(8);
        case 0x35:
        {
            if (!c.skip(16))
            {
                return false;
            }
            uint32_t n = 0;
            return c.get32le(n) && c.skip(n);
        }
        case 0x40:
        {
            uint8_t fmt = 0;
            uint32_t n = 0;
            return c.get8(fmt) && c.get24le(n) && c.skip(n);
        }
        case 0x5A:
            return c.skip(9);
        default:
            log_debug("tape EAR: TZX id 0x%02X length unknown — stopping", id);
            return false;
    }
}

bool TapeDeck::load_tap(const uint8_t* data, size_t size)
{
    reset();
    if (data == nullptr || size < 2)
    {
        return false;
    }
    ByteCursor c(data, size);
    int blocks = 0;
    while (c.remaining() >= 2)
    {
        uint16_t len = 0;
        if (!c.get16le(len))
        {
            break;
        }
        if (c.remaining() < len)
        {
            break;
        }
        if (len == 0)
        {
            continue;
        }
        if (len == 1)
        {
            uint8_t dummy = 0;
            if (!c.get8(dummy))
            {
                break;
            }
            continue;
        }
        uint8_t flag = 0;
        if (!c.get8(flag))
        {
            break;
        }
        const uint16_t payload_len = static_cast<uint16_t>(len - 2);
        const uint8_t* payload = c.peek(payload_len);
        if (payload == nullptr)
        {
            break;
        }
        c.skip(payload_len);
        uint8_t checksum = 0;
        if (!c.get8(checksum))
        {
            break;
        }
        uint8_t x = flag;
        for (uint16_t i = 0; i < payload_len; i++)
        {
            x = static_cast<uint8_t>(x ^ payload[i]);
        }
        x = static_cast<uint8_t>(x ^ checksum);
        if (x != 0)
        {
            log_warn("TAP: checksum mismatch len=%u xor=0x%02X", len, x);
        }
        add_block(flag, payload, payload_len, checksum);
        blocks++;
    }
    if (blocks == 0)
    {
        return false;
    }
    idx_ = 0;
    remain_ = edges_.empty() ? 0 : edges_[0];
    level_ = true;
    loaded_ = true;
    log_info("tape EAR: %d TAP blocks, %zu edges", blocks, edges_.size());
    return true;
}

bool TapeDeck::load_tzx(const uint8_t* data, size_t size)
{
    reset();
    if (data == nullptr || size < 10 || memcmp(data, "ZXTape!", 7) != 0)
    {
        return false;
    }
    ByteCursor c(data + 10, size - 10);
    int blocks = 0;
    while (c.remaining() >= 1)
    {
        uint8_t id = 0;
        if (!c.get8(id))
        {
            break;
        }
        if (id == 0x10)
        {
            uint16_t pause = 0;
            uint16_t len = 0;
            if (!c.get16le(pause) || !c.get16le(len) || len < 2 || c.remaining() < len)
            {
                break;
            }
            (void)pause;
            uint8_t flag = 0;
            if (!c.get8(flag))
            {
                break;
            }
            const uint16_t payload_len = static_cast<uint16_t>(len - 2);
            const uint8_t* payload = c.peek(payload_len);
            if (!c.skip(payload_len))
            {
                break;
            }
            uint8_t checksum = 0;
            if (!c.get8(checksum))
            {
                break;
            }
            add_block(flag, payload, payload_len, checksum);
            blocks++;
            continue;
        }
        if (id == 0x11)
        {
            uint16_t pilot = 0;
            uint16_t sync1 = 0;
            uint16_t sync2 = 0;
            uint16_t zero = 0;
            uint16_t one = 0;
            uint16_t n_pilot = 0;
            uint8_t used_bits = 0;
            uint16_t pause_ms = 0;
            uint32_t n = 0;
            if (!c.get16le(pilot) || !c.get16le(sync1) || !c.get16le(sync2) ||
                !c.get16le(zero) || !c.get16le(one) || !c.get16le(n_pilot) ||
                !c.get8(used_bits) || !c.get16le(pause_ms) || !c.get24le(n))
            {
                break;
            }
            if (c.remaining() < n)
            {
                break;
            }
            const uint8_t* payload = (n == 0) ? c.peek(0) : c.peek(n);
            if (n > 0 && payload == nullptr)
            {
                break;
            }
            if (!c.skip(n))
            {
                break;
            }
            add_pilot(static_cast<int>(n_pilot), pilot);
            add_edge(sync1);
            add_edge(sync2);
            add_data(payload, n, zero, one, used_bits);
            add_edge(static_cast<uint32_t>(pause_ms) * 3500u);
            blocks++;
            continue;
        }
        if (id == 0x14)
        {
            uint16_t zero = 0;
            uint16_t one = 0;
            uint8_t used_bits = 0;
            uint16_t pause_ms = 0;
            uint32_t n = 0;
            if (!c.get16le(zero) || !c.get16le(one) || !c.get8(used_bits) ||
                !c.get16le(pause_ms) || !c.get24le(n))
            {
                break;
            }
            if (c.remaining() < n)
            {
                break;
            }
            const uint8_t* payload = (n == 0) ? c.peek(0) : c.peek(n);
            if (n > 0 && payload == nullptr)
            {
                break;
            }
            if (!c.skip(n))
            {
                break;
            }
            add_data(payload, n, zero, one, used_bits);
            add_edge(static_cast<uint32_t>(pause_ms) * 3500u);
            blocks++;
            continue;
        }
        if (!tzx_skip_id(c, id))
        {
            break;
        }
    }
    if (blocks == 0)
    {
        return false;
    }
    idx_ = 0;
    remain_ = edges_.empty() ? 0 : edges_[0];
    level_ = true;
    loaded_ = true;
    log_info("tape EAR: %d TZX blocks, %zu edges", blocks, edges_.size());
    return true;
}

void TapeDeck::step(int cycles)
{
    if (edges_.empty() || cycles <= 0)
    {
        return;
    }
    uint32_t left = static_cast<uint32_t>(cycles);
    while (left > 0 && idx_ < edges_.size())
    {
        if (remain_ == 0)
        {
            remain_ = edges_[idx_];
        }
        if (left < remain_)
        {
            remain_ -= left;
            return;
        }
        left -= remain_;
        remain_ = 0;
        level_ = !level_;
        idx_++;
        if (idx_ < edges_.size())
        {
            remain_ = edges_[idx_];
        }
    }
}
