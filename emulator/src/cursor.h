/**
 * @file cursor.h
 * @brief Bounds-checked little-endian reader over a memory blob.
 */
#pragma once

#include <cstdint>
#include <cstring>

/** @brief Sequential reader; all getters fail closed (no overrun). */
struct ByteCursor
{
    const uint8_t* p = nullptr;
    size_t n = 0;
    size_t i = 0;

    ByteCursor() = default;
    ByteCursor(const uint8_t* data, size_t size) : p(data), n(size), i(0) {}

    size_t remaining() const
    {
        return (i < n) ? (n - i) : 0;
    }

    bool eof() const
    {
        return remaining() == 0;
    }

    bool skip(size_t k)
    {
        if (remaining() < k)
        {
            return false;
        }
        i += k;
        return true;
    }

    bool get8(uint8_t& out)
    {
        if (remaining() < 1)
        {
            return false;
        }
        out = p[i++];
        return true;
    }

    bool get16le(uint16_t& out)
    {
        uint8_t lo = 0;
        uint8_t hi = 0;
        if (!get8(lo) || !get8(hi))
        {
            return false;
        }
        out = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
        return true;
    }

    bool get24le(uint32_t& out)
    {
        uint8_t b0 = 0;
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        if (!get8(b0) || !get8(b1) || !get8(b2))
        {
            return false;
        }
        out = static_cast<uint32_t>(b0) |
              (static_cast<uint32_t>(b1) << 8) |
              (static_cast<uint32_t>(b2) << 16);
        return true;
    }

    bool get32le(uint32_t& out)
    {
        uint16_t lo = 0;
        uint16_t hi = 0;
        if (!get16le(lo) || !get16le(hi))
        {
            return false;
        }
        out = static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
        return true;
    }

    bool read(uint8_t* dst, size_t len)
    {
        if (remaining() < len)
        {
            return false;
        }
        if (len > 0 && dst != nullptr)
        {
            memcpy(dst, p + i, len);
        }
        i += len;
        return true;
    }

    const uint8_t* peek(size_t len) const
    {
        if (remaining() < len)
        {
            return nullptr;
        }
        return p + i;
    }
};
