/**
 * @file snapshot.cpp
 * @brief Z80/SNA snapshots and TAP/TZX tape fast-load from memory blobs.
 */

#include "snapshot.h"
#include "cursor.h"
#include "log.h"
#include "vfs.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

int z80_page_to_bank(uint8_t page, bool is128k)
{
    if (is128k)
    {
        if (page >= 3 && page <= 10)
        {
            return static_cast<int>(page) - 3;
        }
        return -1;
    }
    if (page == 8)
    {
        return 5;
    }
    if (page == 4)
    {
        return 2;
    }
    if (page == 5)
    {
        return 0;
    }
    return -1;
}

/**
 * @brief Expand Z80 ED-ED RLE into @p dest (v1 marker-terminated or v2/v3 sized).
 * @return true if @p dest_size bytes were produced.
 *
 * A lone 0xED (next byte not 0xED) is one literal. @c ED ED xx yy repeats @c yy
 * @c xx times (@c xx != 0). @c ED ED 00 ends the stream (v1 @c 00 ED ED 00).
 */
static bool decompress_ed_ed(ByteCursor& c, uint8_t* dest, uint32_t dest_size)
{
    uint32_t pos = 0;
    while (pos < dest_size)
    {
        uint8_t b = 0;
        if (!c.get8(b))
        {
            break;
        }
        if (b != 0xED)
        {
            dest[pos++] = b;
            continue;
        }

        uint8_t d = 0;
        if (!c.get8(d))
        {
            dest[pos++] = 0xED;
            break;
        }
        if (d != 0xED)
        {
            dest[pos++] = 0xED;
            if (pos < dest_size)
            {
                dest[pos++] = d;
            }
            continue;
        }

        uint8_t rep = 0;
        if (!c.get8(rep))
        {
            break;
        }
        if (rep == 0x00)
        {
            uint8_t tail = 0;
            (void)c.get8(tail);
            break;
        }
        uint8_t val = 0;
        if (!c.get8(val))
        {
            break;
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(rep) && pos < dest_size; i++)
        {
            dest[pos++] = val;
        }
    }
    return pos >= dest_size;
}

/**
 * @brief Z80 v1 RLE plus terminator @c 00 ED ED 00.
 *
 * Runs of 5+ equal bytes, or 2+ 0xED, become @c ED ED count value. A lone 0xED
 * is one byte; the next byte is never taken into a run (else @c ED ED would be
 * a run header).
 */
static void compress_ed_ed(const uint8_t* src, uint32_t n, std::vector<uint8_t>& out)
{
    bool after_lone_ed = false;
    uint32_t i = 0;
    while (i < n)
    {
        if (after_lone_ed)
        {
            out.push_back(src[i]);
            after_lone_ed = false;
            i++;
            continue;
        }

        const uint8_t v = src[i];
        uint32_t run = 1;
        while (i + run < n && src[i + run] == v && run < 255u)
        {
            run++;
        }

        if (run >= 5u || (v == 0xED && run >= 2u))
        {
            out.push_back(0xED);
            out.push_back(0xED);
            out.push_back(static_cast<uint8_t>(run));
            out.push_back(v);
            i += run;
            continue;
        }

        if (v == 0xED)
        {
            out.push_back(0xED);
            after_lone_ed = true;
            i++;
            continue;
        }

        for (uint32_t k = 0; k < run; k++)
        {
            out.push_back(v);
        }
        i += run;
    }
    out.push_back(0x00);
    out.push_back(0xED);
    out.push_back(0xED);
    out.push_back(0x00);
}

static void parse_z80_v1_header(const uint8_t header[30], Z80& z80)
{
    z80.A = header[0];
    z80.F = header[1];
    z80.C = header[2];
    z80.B = header[3];
    z80.L = header[4];
    z80.H = header[5];
    z80.PC = static_cast<uint16_t>(header[6] | (static_cast<uint16_t>(header[7]) << 8));
    z80.SP = static_cast<uint16_t>(header[8] | (static_cast<uint16_t>(header[9]) << 8));
    z80.I = header[10];
    z80.R = static_cast<uint8_t>((header[11] & 0x7F) | ((header[12] & 1) ? 0x80 : 0));
    z80.E = header[13];
    z80.D = header[14];
    z80.C_ = header[15];
    z80.B_ = header[16];
    z80.E_ = header[17];
    z80.D_ = header[18];
    z80.L_ = header[19];
    z80.H_ = header[20];
    z80.A_ = header[21];
    z80.F_ = header[22];
    z80.IY = static_cast<uint16_t>(header[23] | (static_cast<uint16_t>(header[24]) << 8));
    z80.IX = static_cast<uint16_t>(header[25] | (static_cast<uint16_t>(header[26]) << 8));
    z80.IFF1 = header[27] != 0;
    z80.IFF2 = header[28] != 0;
    z80.IM = header[29] & 3;
}

static void write_48k_linear(ULA& ula, const uint8_t* buf, size_t n)
{
    for (size_t i = 0; i < n && i < 49152; i++)
    {
        ula.write(static_cast<uint16_t>(0x4000 + i), buf[i]);
    }
}

bool load_z80(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 30)
    {
        log_error("Z80: truncated header (%zu bytes)", size);
        return false;
    }

    uint8_t header[30];
    memcpy(header, data, 30);
    parse_z80_v1_header(header, z80);
    const bool compressed = (header[12] & 0x20) != 0;
    ByteCursor c(data + 30, size - 30);

    log_info("Z80 v1-header PC=0x%04X SP=0x%04X IM=%u IFF1=%u compressed=%d size=%zu",
             z80.PC, z80.SP, z80.IM, z80.IFF1 ? 1 : 0, compressed ? 1 : 0, size);

    if (z80.PC != 0)
    {
        /* v1: 48K dump from 0x4000. */
        if (compressed)
        {
            uint8_t buf[49152];
            memset(buf, 0, sizeof(buf));
            if (!decompress_ed_ed(c, buf, 49152))
            {
                log_warn("Z80 v1: decompressor stopped early");
            }
            write_48k_linear(ula, buf, 49152);
        }
        else
        {
            if (c.remaining() < 49152)
            {
                log_error("Z80 v1: uncompressed dump short (%zu)", c.remaining());
                return false;
            }
            write_48k_linear(ula, c.peek(49152), 49152);
        }
        return true;
    }

    uint16_t ext_len = 0;
    if (!c.get16le(ext_len))
    {
        log_error("Z80 v2/v3: missing extra header length");
        return false;
    }
    if (c.remaining() < ext_len)
    {
        log_error("Z80 v2/v3: extra header truncated");
        return false;
    }

    const uint8_t* ext = c.peek(ext_len);
    z80.PC = static_cast<uint16_t>(ext[0] | (static_cast<uint16_t>(ext[1]) << 8));
    const uint8_t hw_mode = ext[2];
    const bool v3 = ext_len >= 54;
    bool is128k = false;
    if (!v3)
    {
        is128k = (hw_mode >= 3);
    }
    else
    {
        is128k = (hw_mode >= 4);
    }
    log_info("Z80 %s PC=0x%04X hw_mode=%u 128K=%d extra=%u",
             v3 ? "v3" : "v2", z80.PC, hw_mode, is128k ? 1 : 0, ext_len);

    if (is128k)
    {
        ula.setModel128(true);
        if (ext_len >= 4)
        {
            ula.port7ffd = ext[3];
        }
        if (ext_len >= 23)
        {
            ula.ay.select(ext[6]);
            for (int i = 0; i < 16; i++)
            {
                ula.ay.writeReg(static_cast<uint8_t>(i), ext[7 + i]);
            }
        }
        if (ext_len >= 55)
        {
            ula.port1ffd = ext[54];
        }
    }
    c.skip(ext_len);

    while (c.remaining() >= 3)
    {
        uint16_t block_len = 0;
        uint8_t page = 0;
        if (!c.get16le(block_len) || !c.get8(page))
        {
            break;
        }
        const int bank = z80_page_to_bank(page, is128k);
        const bool uncompressed = (block_len == 0xFFFF);
        log_debug("Z80 block page=%u bank=%d len=%u uncompressed=%d",
                  page, bank, block_len, uncompressed ? 1 : 0);

        uint8_t pagebuf[16384];
        memset(pagebuf, 0, sizeof(pagebuf));
        if (uncompressed)
        {
            if (!c.read(pagebuf, 16384))
            {
                log_error("Z80: uncompressed page %u short", page);
                return false;
            }
        }
        else
        {
            if (c.remaining() < block_len)
            {
                log_error("Z80: compressed page %u short", page);
                return false;
            }
            ByteCursor block(c.peek(block_len), block_len);
            decompress_ed_ed(block, pagebuf, 16384);
            c.skip(block_len);
        }

        if (bank >= 0 && bank < 8)
        {
            memcpy(ula.ram_banks[bank], pagebuf, 16384);
            /* Keep 48K linear ram[] in sync for banks 5/2/0. */
            if (bank == 5)
            {
                memcpy(ula.ram + 0x0000, pagebuf, 16384);
            }
            else if (bank == 2)
            {
                memcpy(ula.ram + 0x4000, pagebuf, 16384);
            }
            else if (bank == 0)
            {
                memcpy(ula.ram + 0x8000, pagebuf, 16384);
            }
        }
        else
        {
            log_debug("Z80: skipping non-RAM page %u", page);
        }
    }
    return true;
}

bool save_z80(const char* path, const Z80& z80, const ULA& ula)
{
    FILE* f = fopen(path, "wb");
    if (f == nullptr)
    {
        return false;
    }

    uint8_t header[30];
    memset(header, 0, sizeof(header));
    header[0] = z80.A;
    header[1] = z80.F;
    header[2] = z80.C;
    header[3] = z80.B;
    header[4] = z80.L;
    header[5] = z80.H;
    header[6] = static_cast<uint8_t>(z80.PC & 0xFF);
    header[7] = static_cast<uint8_t>(z80.PC >> 8);
    header[8] = static_cast<uint8_t>(z80.SP & 0xFF);
    header[9] = static_cast<uint8_t>(z80.SP >> 8);
    header[10] = z80.I;
    header[11] = static_cast<uint8_t>(z80.R & 0x7F);
    header[12] = static_cast<uint8_t>(0x20 | ((z80.R >> 7) & 1));
    header[13] = z80.E;
    header[14] = z80.D;
    header[15] = z80.C_;
    header[16] = z80.B_;
    header[17] = z80.E_;
    header[18] = z80.D_;
    header[19] = z80.L_;
    header[20] = z80.H_;
    header[21] = z80.A_;
    header[22] = z80.F_;
    header[23] = static_cast<uint8_t>(z80.IY & 0xFF);
    header[24] = static_cast<uint8_t>(z80.IY >> 8);
    header[25] = static_cast<uint8_t>(z80.IX & 0xFF);
    header[26] = static_cast<uint8_t>(z80.IX >> 8);
    header[27] = z80.IFF1 ? 1 : 0;
    header[28] = z80.IFF2 ? 1 : 0;
    header[29] = static_cast<uint8_t>(z80.IM & 3);
    fwrite(header, 1, 30, f);

    uint8_t linear[49152];
    memcpy(linear + 0x0000, ula.ram_banks[5], 16384);
    memcpy(linear + 0x4000, ula.ram_banks[2], 16384);
    memcpy(linear + 0x8000, ula.ram_banks[ula.is128 ? (ula.port7ffd & 7) : 0], 16384);

    std::vector<uint8_t> compressed;
    compressed.reserve(49152);
    compress_ed_ed(linear, 49152, compressed);
    fwrite(compressed.data(), 1, compressed.size(), f);
    fclose(f);
    return true;
}

bool load_sna(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 27 + 49152)
    {
        log_error("SNA: too small (%zu)", size);
        return false;
    }
    const uint8_t* h = data;
    z80.I = h[0];
    z80.L_ = h[1];
    z80.H_ = h[2];
    z80.E_ = h[3];
    z80.D_ = h[4];
    z80.C_ = h[5];
    z80.B_ = h[6];
    z80.F_ = h[7];
    z80.A_ = h[8];
    z80.L = h[9];
    z80.H = h[10];
    z80.E = h[11];
    z80.D = h[12];
    z80.C = h[13];
    z80.B = h[14];
    z80.IY = static_cast<uint16_t>(h[15] | (static_cast<uint16_t>(h[16]) << 8));
    z80.IX = static_cast<uint16_t>(h[17] | (static_cast<uint16_t>(h[18]) << 8));
    z80.IFF1 = (h[19] & 4) != 0;
    z80.IFF2 = z80.IFF1;
    z80.R = h[20];
    z80.F = h[21];
    z80.A = h[22];
    z80.SP = static_cast<uint16_t>(h[23] | (static_cast<uint16_t>(h[24]) << 8));
    z80.IM = h[25] & 3;
    ula.border = h[26] & 7;

    const uint8_t* ram48 = data + 27;
    const bool is128k = (size >= 131103);
    log_info("SNA size=%zu SP=0x%04X border=%u 128K=%d", size, z80.SP, ula.border, is128k ? 1 : 0);

    memcpy(ula.ram_banks[5], ram48 + 0x0000, 16384);
    memcpy(ula.ram_banks[2], ram48 + 0x4000, 16384);
    memcpy(ula.ram_banks[0], ram48 + 0x8000, 16384);
    memcpy(ula.ram, ram48, 49152);

    if (is128k && size >= 49179)
    {
        ula.setModel128(true);
        const uint8_t* tr = data + 27 + 49152;
        z80.PC = static_cast<uint16_t>(tr[0] | (static_cast<uint16_t>(tr[1]) << 8));
        ula.port7ffd = tr[2];
        log_info("SNA 128K PC=0x%04X 7FFD=0x%02X", z80.PC, ula.port7ffd);

        /* Remaining 16K pages: banks not already in 5,2,paged. */
        size_t off = 27 + 49152 + 4;
        const uint8_t paged = static_cast<uint8_t>(ula.port7ffd & 7);
        for (int bank = 0; bank < 8 && off + 16384 <= size; bank++)
        {
            if (bank == 5 || bank == 2 || bank == paged)
            {
                continue;
            }
            memcpy(ula.ram_banks[bank], data + off, 16384);
            off += 16384;
        }
        /* The 16K at C000 in the 48K dump is the currently paged bank. */
        memcpy(ula.ram_banks[paged], ram48 + 0x8000, 16384);
    }
    else
    {
        z80.PC = static_cast<uint16_t>(ula.read(z80.SP) | (static_cast<uint16_t>(ula.read(static_cast<uint16_t>(z80.SP + 1))) << 8));
        z80.SP = static_cast<uint16_t>(z80.SP + 2);
        log_info("SNA 48K PC=0x%04X SP=0x%04X", z80.PC, z80.SP);
    }
    return true;
}

struct TapeHeader
{
    bool valid = false;
    uint8_t type = 0;
    char name[11]{};
    uint16_t length = 0;
    uint16_t param1 = 0;
    uint16_t param2 = 0;
};

static void parse_tape_header(const uint8_t* p, TapeHeader& h)
{
    h.valid = true;
    h.type = p[0];
    memcpy(h.name, p + 1, 10);
    h.name[10] = 0;
    for (int i = 9; i >= 0; i--)
    {
        if (h.name[i] == ' ')
        {
            h.name[i] = 0;
        }
        else
        {
            break;
        }
    }
    h.length = static_cast<uint16_t>(p[11] | (static_cast<uint16_t>(p[12]) << 8));
    h.param1 = static_cast<uint16_t>(p[13] | (static_cast<uint16_t>(p[14]) << 8));
    h.param2 = static_cast<uint16_t>(p[15] | (static_cast<uint16_t>(p[16]) << 8));
}

static void finish_tape(Z80& z80, uint16_t code_start, bool have_code)
{
    z80.IY = 0x5C3A;
    z80.setHL(0x2758);
    z80.setDE(0x0000);
    z80.setBC(0x0000);
    z80.A = 0x00;
    z80.F = 0x44;
    z80.IX = 0xFFFF;
    z80.IFF1 = true;
    z80.IFF2 = true;
    z80.IM = 1;
    z80.SP = 0xFF4A;
    if (have_code)
    {
        z80.PC = code_start;
        log_info("tape fast-load: jump to CODE 0x%04X", code_start);
    }
    else
    {
        z80.PC = 0x0000;
        log_warn("tape fast-load: no CODE block; PC=0 (needs a real ROM for BASIC)");
    }
}

static bool ingest_tape_block(uint8_t flag, const uint8_t* payload, uint16_t payload_len,
                              TapeHeader& pending, uint16_t& code_start, bool& have_code,
                              ULA& ula)
{
    if (flag == 0x00 && payload_len >= 17)
    {
        parse_tape_header(payload, pending);
        const char* kinds[] = {"PROGRAM", "NUMARRAY", "CHARARRAY", "BYTES"};
        const char* k = (pending.type < 4) ? kinds[pending.type] : "OTHER";
        log_info("tape header type=%u (%s) name=\"%s\" len=%u param1=0x%04X param2=0x%04X",
                 pending.type, k, pending.name, pending.length, pending.param1, pending.param2);
        return true;
    }
    if (flag == 0xFF || flag == 0x00)
    {
        uint16_t dest = 0;
        if (pending.valid)
        {
            if (pending.type == 3)
            {
                dest = pending.param1;
            }
            else if (pending.type == 0)
            {
                dest = 0x5CCB;
            }
            else
            {
                dest = pending.param1;
            }
        }
        else
        {
            dest = 0x4000;
        }
        const uint16_t n = payload_len;
        log_info("tape data flag=0x%02X dest=0x%04X len=%u", flag, dest, n);
        for (uint16_t i = 0; i < n; i++)
        {
            const uint32_t a = static_cast<uint32_t>(dest) + i;
            if (a > 0xFFFF)
            {
                break;
            }
            ula.write(static_cast<uint16_t>(a), payload[i]);
        }
        if (pending.valid && pending.type == 3)
        {
            code_start = pending.param1;
            have_code = true;
        }
        pending.valid = false;
        return true;
    }
    log_debug("tape: ignoring flag=0x%02X len=%u", flag, payload_len);
    return true;
}

bool load_tap(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    ByteCursor c(data, size);
    TapeHeader pending;
    uint16_t code_start = 0;
    bool have_code = false;
    int blocks = 0;

    while (c.remaining() >= 2)
    {
        uint16_t len = 0;
        if (!c.get16le(len) || len < 2)
        {
            break;
        }
        if (c.remaining() < len)
        {
            log_error("TAP: block truncated (len=%u remain=%zu)", len, c.remaining());
            return false;
        }
        uint8_t flag = 0;
        if (!c.get8(flag))
        {
            return false;
        }
        const uint16_t payload_len = static_cast<uint16_t>(len - 2);
        const uint8_t* payload = c.peek(payload_len);
        if (payload == nullptr)
        {
            return false;
        }
        c.skip(payload_len);
        uint8_t checksum = 0;
        if (!c.get8(checksum))
        {
            return false;
        }
        (void)checksum;
        ingest_tape_block(flag, payload, payload_len, pending, code_start, have_code, ula);
        blocks++;
    }
    if (blocks == 0)
    {
        log_error("TAP: no blocks");
        return false;
    }
    finish_tape(z80, code_start, have_code);
    ula.tape.load_tap(data, size);
    return true;
}

static int tzx_skip_or_data(ByteCursor& c, uint8_t id, uint8_t& flag,
                            const uint8_t*& payload, uint16_t& payload_len)
{
    switch (id)
    {
        case 0x10:
        {
            uint16_t pause = 0;
            uint16_t len = 0;
            if (!c.get16le(pause) || !c.get16le(len) || len < 2)
            {
                return (len == 0) ? 0 : -1;
            }
            (void)pause;
            if (!c.get8(flag))
            {
                return -1;
            }
            payload_len = static_cast<uint16_t>(len - 2);
            payload = c.peek(payload_len);
            if (payload == nullptr || !c.skip(payload_len))
            {
                return -1;
            }
            uint8_t cs = 0;
            if (!c.get8(cs))
            {
                return -1;
            }
            (void)cs;
            return 1;
        }
        case 0x11:
        {
            /* 5×WORD pulses + pilot count + used-bits + pause + 3-byte length = 17. */
            if (!c.skip(12))
            {
                return -1;
            }
            uint16_t pause = 0;
            uint32_t len = 0;
            if (!c.get16le(pause) || !c.get24le(len))
            {
                return -1;
            }
            (void)pause;
            if (len < 2)
            {
                return c.skip(len) ? 0 : -1;
            }
            if (!c.get8(flag))
            {
                return -1;
            }
            payload_len = static_cast<uint16_t>(len - 2 > 0xFFFF ? 0xFFFF : len - 2);
            payload = c.peek(payload_len);
            if (payload == nullptr || !c.skip(len - 1))
            {
                return -1;
            }
            return 1;
        }
        case 0x12:
            return c.skip(4) ? 0 : -1;
        case 0x13:
        {
            uint8_t n = 0;
            if (!c.get8(n))
            {
                return -1;
            }
            return c.skip(static_cast<size_t>(n) * 2) ? 0 : -1;
        }
        case 0x14:
        {
            /* WORD zero, WORD one, used-bits, pause, 3-byte length = 10. */
            if (!c.skip(4))
            {
                return -1;
            }
            uint8_t bits = 0;
            uint16_t pause = 0;
            uint32_t len = 0;
            if (!c.get8(bits) || !c.get16le(pause) || !c.get24le(len))
            {
                return -1;
            }
            (void)pause;
            if (bits != 8 || len < 2)
            {
                return c.skip(len) ? 0 : -1;
            }
            if (!c.get8(flag))
            {
                return -1;
            }
            payload_len = static_cast<uint16_t>(len - 2 > 0xFFFF ? 0xFFFF : len - 2);
            payload = c.peek(payload_len);
            if (payload == nullptr || !c.skip(len - 1))
            {
                return -1;
            }
            return 1;
        }
        case 0x20:
            return c.skip(2) ? 0 : -1;
        case 0x21:
        {
            uint8_t n = 0;
            return c.get8(n) && c.skip(n) ? 0 : -1;
        }
        case 0x22:
            return 0;
        case 0x24:
            return c.skip(2) ? 0 : -1;
        case 0x25:
        case 0x23:
            return c.skip(2) ? 0 : -1;
        case 0x26:
        {
            uint16_t n = 0;
            return c.get16le(n) && c.skip(static_cast<size_t>(n) * 2) ? 0 : -1;
        }
        case 0x27:
            return 0;
        case 0x28:
        {
            uint16_t n = 0;
            return c.get16le(n) && c.skip(n) ? 0 : -1;
        }
        case 0x2A:
            return c.skip(4) ? 0 : -1;
        case 0x2B:
            return c.skip(5) ? 0 : -1;
        case 0x30:
        {
            uint8_t n = 0;
            if (!c.get8(n))
            {
                return -1;
            }
            const uint8_t* t = c.peek(n);
            if (t != nullptr)
            {
                char buf[256];
                const size_t m = n < 255 ? n : 255;
                memcpy(buf, t, m);
                buf[m] = 0;
                log_info("TZX text: %s", buf);
            }
            return c.skip(n) ? 0 : -1;
        }
        case 0x31:
        {
            uint8_t t = 0;
            uint8_t n = 0;
            return c.get8(t) && c.get8(n) && c.skip(n) ? 0 : -1;
        }
        case 0x32:
        {
            uint16_t n = 0;
            return c.get16le(n) && c.skip(n) ? 0 : -1;
        }
        case 0x33:
        {
            uint8_t n = 0;
            return c.get8(n) && c.skip(static_cast<size_t>(n) * 3) ? 0 : -1;
        }
        case 0x35:
        {
            if (!c.skip(16))
            {
                return -1;
            }
            uint32_t n = 0;
            return c.get32le(n) && c.skip(n) ? 0 : -1;
        }
        case 0x5A:
            return c.skip(9) ? 0 : -1;
        default:
            log_warn("TZX: unhandled block id 0x%02X — stopping parse", id);
            return -1;
    }
}

bool load_tzx(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 10 || memcmp(data, "ZXTape!", 7) != 0)
    {
        log_error("TZX: bad signature");
        return false;
    }
    ByteCursor c(data, size);
    c.skip(8);
    uint8_t major = 0;
    uint8_t minor = 0;
    if (!c.get8(major) || !c.get8(minor))
    {
        return false;
    }
    log_info("TZX v%u.%u (%zu bytes)", major, minor, size);

    TapeHeader pending;
    uint16_t code_start = 0;
    bool have_code = false;
    int blocks = 0;
    while (!c.eof())
    {
        uint8_t id = 0;
        if (!c.get8(id))
        {
            break;
        }
        uint8_t flag = 0;
        const uint8_t* payload = nullptr;
        uint16_t payload_len = 0;
        const int rc = tzx_skip_or_data(c, id, flag, payload, payload_len);
        if (rc < 0)
        {
            break;
        }
        if (rc > 0 && payload != nullptr)
        {
            ingest_tape_block(flag, payload, payload_len, pending, code_start, have_code, ula);
            blocks++;
        }
    }
    if (blocks == 0)
    {
        log_error("TZX: no data blocks");
        return false;
    }
    finish_tape(z80, code_start, have_code);
    ula.tape.load_tzx(data, size);
    return true;
}

bool load_rom_blob(const uint8_t* data, size_t size, ULA& ula)
{
    if (data == nullptr)
    {
        return false;
    }
    if (size == 16384)
    {
        memcpy(ula.rom, data, 16384);
        memcpy(ula.rom1, data, 16384);
        log_info("ROM 16K loaded");
        return true;
    }
    if (size == 32768)
    {
        memcpy(ula.rom, data, 16384);
        memcpy(ula.rom1, data + 16384, 16384);
        log_info("ROM 32K (128K pair) loaded");
        return true;
    }
    if (size == 65536)
    {
        memcpy(ula.rom, data, 16384);
        memcpy(ula.rom1, data + 16384, 16384);
        memcpy(ula.rom2, data + 32768, 16384);
        memcpy(ula.rom3, data + 49152, 16384);
        ula.setPlus3(true);
        log_info("ROM 64K (+3 four banks) loaded");
        return true;
    }
    log_error("ROM: expected 16384, 32768 or 65536 bytes, got %zu", size);
    return false;
}

bool load_rom_file(const char* path, ULA& ula)
{
    VfsBlob blob;
    if (!vfs_read(path, blob))
    {
        return false;
    }
    return load_rom_blob(blob.data.data(), blob.data.size(), ula);
}

bool load_rom_from_dir(const char* dir, ULA& ula)
{
    DIR* d = opendir(dir);
    if (d == nullptr)
    {
        return false;
    }
    static const char* candidates[] = {
        "48.rom", "spectrum.rom", "48K.ROM", "48k.rom",
        "128.rom", "spectrum128.rom", "128K.ROM", "128k.rom"
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, candidates[i]);
        struct stat st{};
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            closedir(d);
            return load_rom_file(path, ula);
        }
    }
    struct dirent* ent = nullptr;
    while ((ent = readdir(d)) != nullptr)
    {
        if (ent->d_name[0] == '.')
        {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st{};
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        {
            continue;
        }
        if (st.st_size == 16384 || st.st_size == 32768 || st.st_size == 65536)
        {
            closedir(d);
            return load_rom_file(path, ula);
        }
    }
    closedir(d);
    return false;
}

bool load_plus3_roms_from_dir(const char* dir, ULA& ula)
{
    if (dir == nullptr)
    {
        return false;
    }
    uint8_t buf[65536];
    for (int i = 0; i < 4; i++)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/plus3-%d.rom", dir, i);
        FILE* f = fopen(path, "rb");
        if (f == nullptr)
        {
            return false;
        }
        const size_t n = fread(buf + static_cast<size_t>(i) * 16384, 1, 16384, f);
        fclose(f);
        f = nullptr;
        if (n != 16384)
        {
            return false;
        }
    }
    return load_rom_blob(buf, 65536, ula);
}

bool load_trdos_rom_file(const char* path, ULA& ula)
{
    if (path == nullptr)
    {
        return false;
    }
    VfsBlob blob;
    if (!vfs_read(path, blob))
    {
        return false;
    }
    if (blob.data.size() != 16384)
    {
        log_error("TR-DOS ROM: expected 16384 bytes at %s (got %zu)", path, blob.data.size());
        return false;
    }
    memcpy(ula.trdos_rom, blob.data.data(), 16384);
    ula.trdos_present = true;
    log_info("TR-DOS ROM loaded from %s", path);
    return true;
}

bool load_system_roms(ULA& ula, const char* model)
{
    const char* dir = "/usr/share/fuse";
    const char* m = (model != nullptr) ? model : "spectrum48";
    if (strcmp(m, "plus3") == 0 || strcmp(m, "spectrum+3") == 0)
    {
        return load_plus3_roms_from_dir(dir, ula);
    }
    if (strcmp(m, "spectrum128") == 0)
    {
        uint8_t buf[32768];
        FILE* f0 = fopen("/usr/share/fuse/128-0.rom", "rb");
        FILE* f1 = fopen("/usr/share/fuse/128-1.rom", "rb");
        if (f0 == nullptr || f1 == nullptr)
        {
            if (f0 != nullptr)
            {
                fclose(f0);
            }
            if (f1 != nullptr)
            {
                fclose(f1);
            }
            return load_rom_from_dir(dir, ula);
        }
        const size_t n0 = fread(buf, 1, 16384, f0);
        const size_t n1 = fread(buf + 16384, 1, 16384, f1);
        fclose(f0);
        f0 = nullptr;
        fclose(f1);
        f1 = nullptr;
        if (n0 != 16384 || n1 != 16384)
        {
            return false;
        }
        return load_rom_blob(buf, 32768, ula);
    }
    return load_rom_from_dir(dir, ula);
}
