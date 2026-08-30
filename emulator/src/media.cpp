/**
 * @file media.cpp
 * @brief Format detection and loaders for ZX-era snapshots, disks, pokes, carts.
 */

#include "media.h"
#include "cursor.h"
#include "log.h"
#include "snapshot.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace
{

std::string ext_of(const std::string& name)
{
    const auto slash = name.find_last_of("/\\");
    const std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
    {
        return {};
    }
    std::string e = base.substr(dot);
    for (char& c : e)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

bool inflate_zlib(const uint8_t* src, size_t nsrc, uint8_t* dst, size_t ndst, size_t* nout)
{
    z_stream s;
    memset(&s, 0, sizeof(s));
    s.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    s.avail_in = static_cast<uInt>(nsrc);
    s.next_out = dst;
    s.avail_out = static_cast<uInt>(ndst);
    if (inflateInit(&s) != Z_OK)
    {
        return false;
    }
    const int r = inflate(&s, Z_FINISH);
    if (nout != nullptr)
    {
        *nout = s.total_out;
    }
    inflateEnd(&s);
    return r == Z_STREAM_END;
}

void poke_addr(ULA& ula, int bank, uint16_t addr, uint8_t val)
{
    if (bank == 8)
    {
        ula.write(addr, val);
        return;
    }
    if (bank >= 0 && bank < 8)
    {
        if (addr < 0x4000)
        {
            return;
        }
        ula.ram_banks[bank][addr & 0x3FFF] = val;
        return;
    }
    ula.write(addr, val);
}

bool load_sp(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (size < 38)
    {
        log_error("SP: too small");
        return false;
    }
    size_t off = 0;
    if (data[0] == 'S' && data[1] == 'P')
    {
        off = 0;
    }
    const uint8_t* h = data + off;
    z80.setBC(static_cast<uint16_t>(h[4] | (h[5] << 8)));
    z80.setDE(static_cast<uint16_t>(h[6] | (h[7] << 8)));
    z80.setHL(static_cast<uint16_t>(h[8] | (h[9] << 8)));
    z80.setAF(static_cast<uint16_t>(h[10] | (h[11] << 8)));
    z80.IX = static_cast<uint16_t>(h[12] | (h[13] << 8));
    z80.IY = static_cast<uint16_t>(h[14] | (h[15] << 8));
    z80.setBC_(static_cast<uint16_t>(h[16] | (h[17] << 8)));
    z80.setDE_(static_cast<uint16_t>(h[18] | (h[19] << 8)));
    z80.setHL_(static_cast<uint16_t>(h[20] | (h[21] << 8)));
    z80.setAF_(static_cast<uint16_t>(h[22] | (h[23] << 8)));
    z80.R = h[24];
    z80.I = h[25];
    const uint16_t status = static_cast<uint16_t>(h[26] | (h[27] << 8));
    z80.SP = static_cast<uint16_t>(h[28] | (h[29] << 8));
    z80.PC = static_cast<uint16_t>(h[30] | (h[31] << 8));
    z80.IFF1 = (status & 1) != 0;
    z80.IFF2 = (status & 4) != 0;
    z80.IM = static_cast<uint8_t>((status >> 1) & 3);
    ula.border = static_cast<uint8_t>((status >> 3) & 7);
    const uint8_t* ram = data + 38;
    const size_t ram_n = (size >= 38 + 49152) ? 49152 : (size - 38);
    for (size_t i = 0; i < ram_n; i++)
    {
        ula.write(static_cast<uint16_t>(0x4000 + i), ram[i]);
    }
    log_info("SP PC=0x%04X SP=0x%04X IM=%u ram=%zu", z80.PC, z80.SP, z80.IM, ram_n);
    return ram_n >= 16384;
}

bool load_szx(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (size < 8 || memcmp(data, "ZXST", 4) != 0)
    {
        log_error("SZX: bad magic");
        return false;
    }
    const uint8_t major = data[4];
    const uint8_t minor = data[5];
    const uint8_t machine = data[6];
    log_info("SZX v%u.%u machine=%u", major, minor, machine);
    if (machine >= 2 && machine != 8 && machine != 9 && machine != 12 && machine != 15)
    {
        ula.setModel128(true);
    }
    ByteCursor c(data + 8, size - 8);
    bool got_z80 = false;
    while (c.remaining() >= 8)
    {
        char id[5] = {0, 0, 0, 0, 0};
        if (!c.read(reinterpret_cast<uint8_t*>(id), 4))
        {
            break;
        }
        uint32_t blen = 0;
        if (!c.get32le(blen))
        {
            break;
        }
        if (c.remaining() < blen)
        {
            log_error("SZX: truncated block %s", id);
            return false;
        }
        const uint8_t* payload = c.peek(blen);
        c.skip(blen);
        log_debug("SZX block %s len=%u", id, blen);

        if (memcmp(id, "Z80R", 4) == 0 && blen >= 28)
        {
            auto u16 = [&](size_t o) {
                return static_cast<uint16_t>(payload[o] | (payload[o + 1] << 8));
            };
            z80.setAF(u16(0));
            z80.setBC(u16(2));
            z80.setDE(u16(4));
            z80.setHL(u16(6));
            z80.setAF_(u16(8));
            z80.setBC_(u16(10));
            z80.setDE_(u16(12));
            z80.setHL_(u16(14));
            z80.IX = u16(16);
            z80.IY = u16(18);
            z80.SP = u16(20);
            z80.PC = u16(22);
            z80.I = payload[24];
            z80.R = payload[25];
            z80.IFF1 = payload[26] != 0;
            z80.IFF2 = payload[27] != 0;
            if (blen > 28)
            {
                z80.IM = payload[28] & 3;
            }
            got_z80 = true;
            log_info("SZX Z80R PC=0x%04X SP=0x%04X IM=%u", z80.PC, z80.SP, z80.IM);
        }
        else if (memcmp(id, "SPCR", 4) == 0 && blen >= 3)
        {
            ula.border = payload[0] & 7;
            ula.port7ffd = payload[1];
            if (blen >= 4)
            {
                ula.port1ffd = payload[2];
            }
            log_info("SZX SPCR border=%u 7FFD=0x%02X", ula.border, ula.port7ffd);
        }
        else if (memcmp(id, "RAMP", 4) == 0 && blen >= 3)
        {
            const uint16_t flags = static_cast<uint16_t>(payload[0] | (payload[1] << 8));
            const uint8_t page = payload[2];
            const uint8_t* src = payload + 3;
            const size_t src_n = blen - 3;
            uint8_t pagebuf[16384];
            memset(pagebuf, 0, sizeof(pagebuf));
            if (flags & 1)
            {
                size_t nout = 0;
                if (!inflate_zlib(src, src_n, pagebuf, 16384, &nout))
                {
                    log_error("SZX: zlib inflate failed for page %u", page);
                    return false;
                }
            }
            else
            {
                const size_t n = src_n < 16384 ? src_n : 16384;
                memcpy(pagebuf, src, n);
            }
            if (page < 8)
            {
                memcpy(ula.ram_banks[page], pagebuf, 16384);
                if (page == 5)
                {
                    memcpy(ula.ram + 0x0000, pagebuf, 16384);
                }
                else if (page == 2)
                {
                    memcpy(ula.ram + 0x4000, pagebuf, 16384);
                }
                else if (page == 0)
                {
                    memcpy(ula.ram + 0x8000, pagebuf, 16384);
                }
            }
            log_debug("SZX RAMP page=%u compressed=%d", page, flags & 1);
        }
        else if (memcmp(id, "AY\0\0", 4) == 0 && blen >= 17)
        {
            /* flags + 16 registers (ZXSTAYBLOCK). */
            for (int i = 0; i < 16; i++)
            {
                ula.ay.writeReg(static_cast<uint8_t>(i), payload[1 + i]);
            }
        }
    }
    if (!got_z80)
    {
        log_error("SZX: missing Z80R block");
        return false;
    }
    return true;
}

struct TrdosFile
{
    char name[9]{};
    char ext = 0;
    uint16_t start = 0;
    uint16_t length = 0;
    uint8_t sectors = 0;
    const uint8_t* body = nullptr;
};

void load_trdos_files(const std::vector<TrdosFile>& files, Z80& z80, ULA& ula)
{
    uint16_t jump = 0;
    bool have_jump = false;
    uint16_t biggest = 0;
    uint16_t biggest_start = 0;
    for (const auto& f : files)
    {
        log_info("TR-DOS file \"%s.%c\" start=0x%04X len=%u sectors=%u",
                 f.name, f.ext ? f.ext : '?', f.start, f.length, f.sectors);
        if (f.body == nullptr || f.length == 0)
        {
            continue;
        }
        const char ext = static_cast<char>(std::toupper(static_cast<unsigned char>(f.ext)));
        uint16_t dest = f.start;
        if (ext == 'B')
        {
            dest = 0x5CCB;
        }
        if (dest < 0x4000 && ext != 'B')
        {
            /* CODE in contended/low RAM still allowed if start >= 0x4000. */
        }
        for (uint16_t i = 0; i < f.length; i++)
        {
            const uint32_t a = static_cast<uint32_t>(dest) + i;
            if (a > 0xFFFF)
            {
                break;
            }
            ula.write(static_cast<uint16_t>(a), f.body[i]);
        }
        if (ext == 'C' && f.length > biggest)
        {
            biggest = f.length;
            biggest_start = f.start;
            have_jump = true;
        }
        std::string nm = vfs_lower(f.name);
        if (nm.find("boot") != std::string::npos && ext == 'C')
        {
            jump = f.start;
            have_jump = true;
        }
    }
    if (jump == 0 && have_jump)
    {
        jump = biggest_start;
    }
    z80.IY = 0x5C3A;
    z80.IFF1 = z80.IFF2 = true;
    z80.IM = 1;
    z80.SP = 0xFF4A;
    if (have_jump)
    {
        z80.PC = jump;
        log_info("TR-DOS fast-load jump 0x%04X", jump);
    }
    else
    {
        z80.PC = 0x0000;
        log_warn("TR-DOS: no CODE file to jump to (needs TR-DOS ROM for BASIC boot)");
    }
}

bool load_scl(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (size < 9 || memcmp(data, "SINCLAIR", 8) != 0)
    {
        log_error("SCL: bad magic");
        return false;
    }
    const uint8_t nfiles = data[8];
    const size_t dir_bytes = static_cast<size_t>(nfiles) * 14;
    if (size < 9 + dir_bytes)
    {
        log_error("SCL: directory truncated");
        return false;
    }
    log_info("SCL files=%u", nfiles);
    std::vector<TrdosFile> files;
    size_t body = 9 + dir_bytes;
    for (uint8_t i = 0; i < nfiles; i++)
    {
        const uint8_t* e = data + 9 + static_cast<size_t>(i) * 14;
        TrdosFile f;
        memcpy(f.name, e, 8);
        f.name[8] = 0;
        for (int k = 7; k >= 0; k--)
        {
            if (f.name[k] == ' ')
            {
                f.name[k] = 0;
            }
            else
            {
                break;
            }
        }
        f.ext = static_cast<char>(e[8]);
        /* SCL dirent = TR-DOS 16-byte entry without track/sector: start, length, sectors. */
        f.start = static_cast<uint16_t>(e[9] | (e[10] << 8));
        f.length = static_cast<uint16_t>(e[11] | (e[12] << 8));
        f.sectors = e[13];
        const size_t nbytes = static_cast<size_t>(f.sectors) * 256;
        if (body + nbytes > size)
        {
            log_error("SCL: file body truncated (%s)", f.name);
            return false;
        }
        f.body = data + body;
        if (f.length == 0 || f.length > nbytes)
        {
            f.length = static_cast<uint16_t>(nbytes > 0xFFFF ? 0xFFFF : nbytes);
        }
        body += nbytes;
        files.push_back(f);
    }
    ula.setModel128(true);
    load_trdos_files(files, z80, ula);
    return true;
}

bool load_trd(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (size < 2048)
    {
        log_error("TRD: too small");
        return false;
    }
    std::vector<TrdosFile> files;
    for (int i = 0; i < 128; i++)
    {
        const uint8_t* e = data + static_cast<size_t>(i) * 16;
        if (e[0] == 0x00)
        {
            break;
        }
        if (e[0] == 0x01)
        {
            continue; /* deleted */
        }
        TrdosFile f;
        memcpy(f.name, e, 8);
        f.name[8] = 0;
        for (int k = 7; k >= 0; k--)
        {
            if (f.name[k] == ' ')
            {
                f.name[k] = 0;
            }
            else
            {
                break;
            }
        }
        f.ext = static_cast<char>(e[8]);
        f.start = static_cast<uint16_t>(e[9] | (e[10] << 8));
        f.length = static_cast<uint16_t>(e[11] | (e[12] << 8));
        f.sectors = e[13];
        const uint8_t sec = e[14];
        const uint8_t trk = e[15];
        const size_t off = (static_cast<size_t>(trk) * 16 + sec) * 256;
        const size_t nbytes = static_cast<size_t>(f.sectors) * 256;
        if (off + nbytes <= size)
        {
            f.body = data + off;
        }
        files.push_back(f);
    }
    log_info("TRD catalog files=%zu size=%zu", files.size(), size);
    ula.setModel128(true);
    load_trdos_files(files, z80, ula);
    return !files.empty();
}

bool load_dsk(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    (void)z80;
    (void)ula;
    if (size < 256)
    {
        log_error("DSK: too small");
        return false;
    }
    const bool ext = memcmp(data, "EXTENDED", 8) == 0;
    const bool std = memcmp(data, "MV - CPC", 8) == 0;
    if (!ext && !std)
    {
        log_error("DSK: not a CPC/+3 disk image");
        return false;
    }
    log_warn("DSK: +3 FDC not yet emulated (%s, %zu bytes) — image identified, load skipped",
             ext ? "EDSK" : "standard", size);
    return false;
}

bool load_mgt(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    (void)data;
    (void)z80;
    (void)ula;
    log_warn("MGT: DISCiPLE/+D image recognised (%zu bytes) but G+DOS is not emulated yet", size);
    return false;
}

bool load_mdr(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    (void)data;
    (void)z80;
    (void)ula;
    log_warn("MDR: Microdrive cartridge recognised (%zu bytes) but Interface 1 is not emulated yet", size);
    return false;
}

bool load_dck(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (size < 9)
    {
        log_error("DCK: too small");
        return false;
    }
    log_info("DCK Timex dock type=0x%02X size=%zu", data[0], size);
    /* Bank flags at bytes 1–8; 8K chunks follow. Load into 0x0000+ as ROM overlay. */
    size_t off = 9;
    for (int b = 0; b < 8 && off < size; b++)
    {
        const uint8_t flags = data[1 + b];
        if (flags == 0)
        {
            continue;
        }
        const size_t chunk = (off + 8192 <= size) ? 8192 : (size - off);
        const uint16_t dest = static_cast<uint16_t>(b * 8192);
        for (size_t i = 0; i < chunk; i++)
        {
            const uint16_t a = static_cast<uint16_t>(dest + i);
            if (a < 0x4000)
            {
                ula.rom[a] = data[off + i];
            }
            else
            {
                ula.write(a, data[off + i]);
            }
        }
        off += 8192;
        log_debug("DCK bank %d flags=0x%02X dest=0x%04X", b, flags, dest);
    }
    z80.reset();
    z80.PC = 0x0000;
    z80.SP = 0xFFFF;
    z80.IM = 1;
    return true;
}

bool load_fdi(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    (void)data;
    (void)z80;
    (void)ula;
    log_warn("FDI: image recognised (%zu bytes); full Beta Disk FDC not emulated — try .scl/.trd", size);
    return false;
}

bool load_udi(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    (void)data;
    (void)z80;
    (void)ula;
    log_warn("UDI: image recognised (%zu bytes); full Beta Disk FDC not emulated — try .scl/.trd", size);
    return false;
}

bool load_csw(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    (void)z80;
    (void)ula;
    if (size < 32 || memcmp(data, "Compressed Square Wave", 22) != 0)
    {
        log_error("CSW: bad header");
        return false;
    }
    log_warn("CSW: tape pulse dump recognised (%zu bytes); use TAP/TZX for fast-load", size);
    return false;
}

} // namespace

std::string media_detect(const VfsBlob& blob)
{
    const std::string e = ext_of(blob.name.empty() ? blob.path : blob.name);
    if (e == ".tap" || e == ".tzx" || e == ".z80" || e == ".sna" || e == ".szx" ||
        e == ".sp" || e == ".slt" || e == ".trd" || e == ".scl" || e == ".dsk" ||
        e == ".mgt" || e == ".fdi" || e == ".udi" || e == ".mdr" || e == ".dck" ||
        e == ".rom" || e == ".csw" || e == ".d80" || e == ".d40" || e == ".ipf" ||
        e == ".spg" || e == ".pok")
    {
        return e.substr(1);
    }
    if (blob.data.size() >= 8 && memcmp(blob.data.data(), "ZXTape!", 7) == 0)
    {
        return "tzx";
    }
    if (blob.data.size() >= 4 && memcmp(blob.data.data(), "ZXST", 4) == 0)
    {
        return "szx";
    }
    if (blob.data.size() >= 8 && memcmp(blob.data.data(), "SINCLAIR", 8) == 0)
    {
        return "scl";
    }
    if (blob.data.size() >= 2 && blob.data[0] == 'S' && blob.data[1] == 'P')
    {
        return "sp";
    }
    if (blob.data.size() >= 8 &&
        (memcmp(blob.data.data(), "EXTENDED", 8) == 0 ||
         memcmp(blob.data.data(), "MV - CPC", 8) == 0))
    {
        return "dsk";
    }
    return {};
}

bool media_apply_pok(const VfsBlob& blob, ULA& ula)
{
    std::string text(blob.data.begin(), blob.data.end());
    size_t pos = 0;
    int applied = 0;
    while (pos < text.size())
    {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos)
        {
            nl = text.size();
        }
        std::string line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        pos = nl + 1;
        if (line.empty())
        {
            continue;
        }
        const char kind = line[0];
        if (kind == 'N' || kind == 'n')
        {
            log_info("POK trainer: %s", line.c_str() + 1);
            continue;
        }
        if (kind == 'Y' || kind == 'y')
        {
            break;
        }
        if (kind == 'M' || kind == 'm' || kind == 'Z' || kind == 'z')
        {
            int bank = 8;
            int addr = 0;
            int val = 0;
            int orig = 0;
            if (sscanf(line.c_str() + 1, "%d %d %d %d", &bank, &addr, &val, &orig) < 3)
            {
                log_warn("POK: bad line: %s", line.c_str());
                continue;
            }
            poke_addr(ula, bank, static_cast<uint16_t>(addr & 0xFFFF), static_cast<uint8_t>(val & 0xFF));
            log_debug("POK bank=%d addr=0x%04X val=0x%02X orig=0x%02X", bank, addr, val, orig);
            applied++;
        }
    }
    log_info("POK applied %d pokes", applied);
    return applied > 0;
}

bool media_load(const VfsBlob& blob, Z80& z80, ULA& ula)
{
    const std::string fmt = media_detect(blob);
    log_info("media load format=%s name=%s bytes=%zu",
             fmt.empty() ? "?" : fmt.c_str(), blob.name.c_str(), blob.data.size());
    if (fmt.empty())
    {
        log_error("unsupported format: %s", blob.path.c_str());
        return false;
    }
    const uint8_t* p = blob.data.data();
    const size_t n = blob.data.size();

    if (fmt == "z80")
    {
        return load_z80(p, n, z80, ula);
    }
    if (fmt == "slt")
    {
        /* SLT is a Z80 snapshot plus optional level data at the end. */
        return load_z80(p, n, z80, ula);
    }
    if (fmt == "sna")
    {
        return load_sna(p, n, z80, ula);
    }
    if (fmt == "tap")
    {
        return load_tap(p, n, z80, ula);
    }
    if (fmt == "tzx")
    {
        return load_tzx(p, n, z80, ula);
    }
    if (fmt == "szx")
    {
        return load_szx(p, n, z80, ula);
    }
    if (fmt == "sp")
    {
        return load_sp(p, n, z80, ula);
    }
    if (fmt == "scl")
    {
        return load_scl(p, n, z80, ula);
    }
    if (fmt == "trd")
    {
        return load_trd(p, n, z80, ula);
    }
    if (fmt == "dsk")
    {
        return load_dsk(p, n, z80, ula);
    }
    if (fmt == "mgt")
    {
        return load_mgt(p, n, z80, ula);
    }
    if (fmt == "mdr")
    {
        return load_mdr(p, n, z80, ula);
    }
    if (fmt == "dck")
    {
        return load_dck(p, n, z80, ula);
    }
    if (fmt == "fdi")
    {
        return load_fdi(p, n, z80, ula);
    }
    if (fmt == "udi")
    {
        return load_udi(p, n, z80, ula);
    }
    if (fmt == "csw")
    {
        return load_csw(p, n, z80, ula);
    }
    if (fmt == "rom")
    {
        if (!load_rom_blob(p, n, ula))
        {
            return false;
        }
        z80.reset();
        z80.PC = 0x0000;
        z80.SP = 0xFFFF;
        z80.IM = 1;
        return true;
    }
    if (fmt == "pok")
    {
        log_error("POK is a cheat file — load a game first, then --pok FILE");
        return false;
    }
    if (fmt == "d80" || fmt == "d40" || fmt == "ipf" || fmt == "spg")
    {
        log_warn("%s: recognised but not loaded yet (%zu bytes)", fmt.c_str(), n);
        return false;
    }
    log_error("unhandled format %s", fmt.c_str());
    return false;
}

const char* media_format_help()
{
    return "z80 sna szx sp slt tap tzx scl trd dsk mgt mdr dck rom pok fdi udi csw d80 d40 ipf spg zip";
}
