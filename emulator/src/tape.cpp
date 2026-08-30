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

void TapeDeck::add_block(uint8_t flag, const uint8_t* payload, uint16_t payload_len, uint8_t checksum)
{
    const int pilot = (flag == 0x00) ? 8063 : 3223;
    add_pilot(pilot, kPilot);
    add_edge(kSync1);
    add_edge(kSync2);

    auto add_byte = [this](uint8_t b) {
        for (int i = 7; i >= 0; i--)
        {
            const uint32_t p = (b & (1u << i)) ? kBit1 : kBit0;
            add_edge(p);
            add_edge(p);
        }
    };
    add_byte(flag);
    for (uint16_t i = 0; i < payload_len; i++)
    {
        add_byte(payload[i]);
    }
    add_byte(checksum);
    add_edge(kPause);
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
        if (!c.get16le(len) || len < 2 || c.remaining() < len)
        {
            break;
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
            c.skip(payload_len);
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
            /* Turbo: skip 18-byte header then data; encode as standard for EAR. */
            if (c.remaining() < 18)
            {
                break;
            }
            c.skip(15);
            uint16_t len = 0;
            uint8_t lenhi = 0;
            if (!c.get16le(len) || !c.get8(lenhi))
            {
                break;
            }
            const uint32_t n = static_cast<uint32_t>(len) | (static_cast<uint32_t>(lenhi) << 16);
            if (n < 2 || c.remaining() < n)
            {
                break;
            }
            uint8_t flag = 0;
            if (!c.get8(flag))
            {
                break;
            }
            const uint16_t payload_len = static_cast<uint16_t>(n - 2 > 0xFFFF ? 0xFFFF : n - 2);
            const uint8_t* payload = c.peek(payload_len);
            c.skip(n - 1);
            add_block(flag, payload, payload_len, 0);
            blocks++;
            continue;
        }
        if (id == 0x14)
        {
            if (c.remaining() < 10)
            {
                break;
            }
            c.skip(7);
            uint16_t len = 0;
            uint8_t lenhi = 0;
            if (!c.get16le(len) || !c.get8(lenhi))
            {
                break;
            }
            const uint32_t n = static_cast<uint32_t>(len) | (static_cast<uint32_t>(lenhi) << 16);
            if (c.remaining() < n)
            {
                break;
            }
            const uint8_t* payload = c.peek(static_cast<size_t>(n));
            c.skip(n);
            add_block(0xFF, payload, static_cast<uint16_t>(n > 0xFFFF ? 0xFFFF : n), 0);
            blocks++;
            continue;
        }
        /* Same skip table as load_tzx in snapshot.cpp — unknown id stops. */
        if (id == 0x12)
        {
            if (c.remaining() < 4)
            {
                break;
            }
            c.skip(4);
            continue;
        }
        if (id == 0x13)
        {
            uint8_t n = 0;
            if (!c.get8(n) || c.remaining() < static_cast<size_t>(n) * 2)
            {
                break;
            }
            c.skip(static_cast<size_t>(n) * 2);
            continue;
        }
        if (id == 0x20)
        {
            if (c.remaining() < 2)
            {
                break;
            }
            c.skip(2);
            continue;
        }
        if (id == 0x21)
        {
            uint8_t n = 0;
            if (!c.get8(n) || c.remaining() < n)
            {
                break;
            }
            c.skip(n);
            continue;
        }
        if (id == 0x22)
        {
            continue;
        }
        if (id == 0x30)
        {
            uint8_t n = 0;
            if (!c.get8(n) || c.remaining() < n)
            {
                break;
            }
            c.skip(n);
            continue;
        }
        if (id == 0x32)
        {
            uint16_t n = 0;
            if (!c.get16le(n) || c.remaining() < n)
            {
                break;
            }
            c.skip(n);
            continue;
        }
        if (id == 0x5A)
        {
            if (c.remaining() < 9)
            {
                break;
            }
            c.skip(9);
            continue;
        }
        log_debug("tape EAR: TZX id 0x%02X not encoded", id);
        break;
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
