/**
 * @file disk.cpp
 * @brief EDSK/+3, MGT, FDI, MDR, D80, SPG loaders and FDC state machines.
 */

#include "disk.h"

#include "log.h"
#include "ula.h"
#include "z80.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

void DiskMap::put(uint8_t c, uint8_t h, uint8_t r, const uint8_t* p, size_t n)
{
    if (p == nullptr || n == 0)
    {
        return;
    }
    SectorId id{c, h, r};
    sectors[id].assign(p, p + n);
}

const uint8_t* DiskMap::find(uint8_t c, uint8_t h, uint8_t r, size_t* len) const
{
    SectorId id{c, h, r};
    const auto it = sectors.find(id);
    if (it == sectors.end())
    {
        if (len != nullptr)
        {
            *len = 0;
        }
        return nullptr;
    }
    if (len != nullptr)
    {
        *len = it->second.size();
    }
    return it->second.data();
}

namespace
{

void cpu_ready(Z80& z80)
{
    z80.IY = 0x5C3A;
    z80.IFF1 = z80.IFF2 = true;
    z80.IM = 1;
    z80.SP = 0xFF4A;
}

void inject(ULA& ula, uint16_t dest, const uint8_t* p, size_t n)
{
    if (p == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < n; i++)
    {
        const uint32_t a = static_cast<uint32_t>(dest) + static_cast<uint32_t>(i);
        if (a > 0xFFFF)
        {
            break;
        }
        ula.write(static_cast<uint16_t>(a), p[i]);
    }
}

bool parse_edsk(const uint8_t* data, size_t size, DiskMap& map)
{
    if (data == nullptr || size < 256)
    {
        return false;
    }
    const bool ext = memcmp(data, "EXTENDED", 8) == 0;
    const bool std = memcmp(data, "MV - CPC", 8) == 0;
    if (!ext && !std)
    {
        return false;
    }
    const int tracks = data[0x30];
    const int sides = data[0x31] == 0 ? 1 : data[0x31];
    const uint16_t std_tsz = static_cast<uint16_t>(data[0x32] | (data[0x33] << 8));
    size_t pos = 0x100;
    const int ntrk = tracks * sides;
    for (int i = 0; i < ntrk; i++)
    {
        uint32_t tsz = ext ? static_cast<uint32_t>(data[0x34 + i]) * 256u : std_tsz;
        if (tsz == 0)
        {
            continue;
        }
        if (pos + tsz > size)
        {
            break;
        }
        const uint8_t* trk = data + pos;
        pos += tsz;
        if (tsz < 256 || memcmp(trk, "Track-Info", 10) != 0)
        {
            continue;
        }
        const uint8_t ns = trk[0x15];
        size_t off = 0x18;
        struct Sec
        {
            uint8_t c, h, r, n;
            uint16_t len;
        };
        std::vector<Sec> secs;
        secs.reserve(ns);
        for (uint8_t s = 0; s < ns; s++)
        {
            if (off + 8 > tsz)
            {
                break;
            }
            Sec sc{};
            sc.c = trk[off];
            sc.h = trk[off + 1];
            sc.r = trk[off + 2];
            sc.n = trk[off + 3];
            sc.len = static_cast<uint16_t>(trk[off + 6] | (trk[off + 7] << 8));
            secs.push_back(sc);
            off += 8;
        }
        size_t bpos = 256;
        for (const Sec& sc : secs)
        {
            uint32_t slen = sc.len != 0 ? sc.len : (128u << (sc.n & 7));
            if (bpos + slen > tsz)
            {
                break;
            }
            map.put(sc.c, sc.h, sc.r, trk + bpos, slen);
            bpos += slen;
        }
    }
    return !map.empty();
}

bool name_ok(const uint8_t* n11)
{
    int ok = 0;
    for (int i = 0; i < 11; i++)
    {
        const uint8_t c = static_cast<uint8_t>(n11[i] & 0x7F);
        if (c == 0x20)
        {
            continue;
        }
        if (c < 0x20 || c >= 0x7F)
        {
            return false;
        }
        ok++;
    }
    return ok > 0;
}

struct CpmFile
{
    char name[13]{};
    std::vector<uint8_t> blocks;
    uint8_t rc = 0;
};

void gather_cpm(const DiskMap& map, int dir_track, std::vector<CpmFile>& out)
{
    std::vector<uint8_t> ids;
    for (const auto& kv : map.sectors)
    {
        if (kv.first.c == static_cast<uint8_t>(dir_track) && kv.first.h == 0)
        {
            ids.push_back(kv.first.r);
        }
    }
    std::sort(ids.begin(), ids.end());
    std::vector<uint8_t> cat;
    for (uint8_t r : ids)
    {
        size_t n = 0;
        const uint8_t* p = map.find(static_cast<uint8_t>(dir_track), 0, r, &n);
        if (p != nullptr && n > 0)
        {
            cat.insert(cat.end(), p, p + n);
        }
    }
    std::map<std::string, CpmFile> files;
    for (size_t i = 0; i + 32 <= cat.size(); i += 32)
    {
        const uint8_t* e = cat.data() + i;
        if (e[0] == 0xE5 || e[0] > 15)
        {
            continue;
        }
        if (!name_ok(e + 1))
        {
            continue;
        }
        char nm[13];
        int w = 0;
        for (int k = 0; k < 8; k++)
        {
            const char c = static_cast<char>(e[1 + k] & 0x7F);
            if (c != ' ')
            {
                nm[w++] = c;
            }
        }
        nm[w++] = '.';
        for (int k = 0; k < 3; k++)
        {
            const char c = static_cast<char>(e[9 + k] & 0x7F);
            if (c != ' ')
            {
                nm[w++] = c;
            }
        }
        nm[w] = 0;
        CpmFile& f = files[nm];
        if (f.name[0] == 0)
        {
            std::memcpy(f.name, nm, 13);
        }
        f.rc = static_cast<uint8_t>(f.rc + e[15]);
        for (int a = 16; a < 32; a++)
        {
            if (e[a] != 0)
            {
                f.blocks.push_back(e[a]);
            }
        }
    }
    for (auto& kv : files)
    {
        out.push_back(std::move(kv.second));
    }
}

std::vector<std::vector<uint8_t>> linear_512(const DiskMap& map, int reserved_tracks)
{
    std::vector<std::vector<uint8_t>> lin;
    if (map.sectors.empty())
    {
        return lin;
    }
    int max_c = 0;
    int max_h = 0;
    for (const auto& kv : map.sectors)
    {
        max_c = std::max(max_c, static_cast<int>(kv.first.c));
        max_h = std::max(max_h, static_cast<int>(kv.first.h));
    }
    for (int c = reserved_tracks; c <= max_c; c++)
    {
        for (int h = 0; h <= max_h; h++)
        {
            std::vector<uint8_t> rs;
            for (const auto& kv : map.sectors)
            {
                if (kv.first.c == static_cast<uint8_t>(c) && kv.first.h == static_cast<uint8_t>(h))
                {
                    rs.push_back(kv.first.r);
                }
            }
            std::sort(rs.begin(), rs.end());
            for (uint8_t r : rs)
            {
                size_t n = 0;
                const uint8_t* p = map.find(static_cast<uint8_t>(c), static_cast<uint8_t>(h), r, &n);
                if (p == nullptr)
                {
                    continue;
                }
                std::vector<uint8_t> sec(p, p + n);
                if (sec.size() < 512)
                {
                    sec.resize(512, 0);
                }
                lin.push_back(std::move(sec));
            }
        }
    }
    return lin;
}

std::vector<uint8_t> cpm_bytes(const CpmFile& f, const std::vector<std::vector<uint8_t>>& lin, int bls)
{
    const int spb = bls / 512;
    std::vector<uint8_t> out;
    for (uint8_t b : f.blocks)
    {
        const int off = static_cast<int>(b) * spb;
        for (int s = 0; s < spb; s++)
        {
            const int i = off + s;
            if (i < 0 || i >= static_cast<int>(lin.size()))
            {
                out.insert(out.end(), 512, 0);
            }
            else
            {
                out.insert(out.end(), lin[static_cast<size_t>(i)].begin(), lin[static_cast<size_t>(i)].end());
            }
        }
    }
    const size_t rec = static_cast<size_t>(f.rc) * 128u;
    if (rec > 0 && rec < out.size())
    {
        out.resize(rec);
    }
    return out;
}

bool plus3_inject(const std::vector<uint8_t>& file, Z80& z80, ULA& ula, uint16_t& jump, bool& have)
{
    if (file.size() < 128 || memcmp(file.data(), "PLUS3DOS", 8) != 0)
    {
        return false;
    }
    const uint8_t type = file[15];
    const uint16_t len = static_cast<uint16_t>(file[16] | (file[17] << 8));
    const uint16_t start = static_cast<uint16_t>(file[18] | (file[19] << 8));
    const uint8_t* body = file.data() + 128;
    const size_t avail = file.size() - 128;
    const size_t n = std::min(static_cast<size_t>(len), avail);
    uint16_t dest = start;
    if (type == 0)
    {
        dest = 0x5CCB;
    }
    inject(ula, dest, body, n);
    log_info("PLUS3DOS type=%u start=0x%04X len=%u", type, dest, static_cast<unsigned>(n));
    if (type == 3)
    {
        jump = start;
        have = true;
        z80.PC = start;
    }
    return true;
}

void scan_plus3_raw(const DiskMap& map, Z80& z80, ULA& ula, uint16_t& jump, bool& have)
{
    for (const auto& kv : map.sectors)
    {
        const auto& sec = kv.second;
        for (size_t off = 0; off + 128 <= sec.size(); off += 128)
        {
            if (memcmp(sec.data() + off, "PLUS3DOS", 8) != 0)
            {
                continue;
            }
            std::vector<uint8_t> fake(sec.begin() + static_cast<std::ptrdiff_t>(off), sec.end());
            /* Body may continue in later sectors of the same file; use this sector as a minimum. */
            plus3_inject(fake, z80, ula, jump, have);
        }
    }
}

void trdos_from_image(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 2048)
    {
        return;
    }
    uint16_t jump = 0;
    bool have = false;
    uint16_t biggest = 0;
    uint16_t biggest_start = 0;
    for (int i = 0; i < 128; i++)
    {
        const uint8_t* e = data + static_cast<size_t>(i) * 16;
        if (e[0] == 0x00)
        {
            break;
        }
        if (e[0] == 0x01)
        {
            continue;
        }
        char name[9];
        std::memcpy(name, e, 8);
        name[8] = 0;
        const char ext = static_cast<char>(e[8]);
        const uint16_t start = static_cast<uint16_t>(e[9] | (e[10] << 8));
        uint16_t length = static_cast<uint16_t>(e[11] | (e[12] << 8));
        const uint8_t secs = e[13];
        const uint8_t sec = e[14];
        const uint8_t trk = e[15];
        const size_t off = (static_cast<size_t>(trk) * 16 + sec) * 256;
        const size_t nbytes = static_cast<size_t>(secs) * 256;
        if (off + nbytes > size)
        {
            continue;
        }
        if (length == 0 || length > nbytes)
        {
            length = static_cast<uint16_t>(nbytes > 0xFFFF ? 0xFFFF : nbytes);
        }
        uint16_t dest = start;
        if (static_cast<char>(std::toupper(static_cast<unsigned char>(ext))) == 'B')
        {
            dest = 0x5CCB;
        }
        inject(ula, dest, data + off, length);
        if (static_cast<char>(std::toupper(static_cast<unsigned char>(ext))) == 'C' && length > biggest)
        {
            biggest = length;
            biggest_start = start;
            have = true;
        }
        std::string nm = name;
        for (char& c : nm)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (nm.find("boot") != std::string::npos &&
            static_cast<char>(std::toupper(static_cast<unsigned char>(ext))) == 'C')
        {
            jump = start;
            have = true;
        }
        log_info("disk TR-DOS \"%s.%c\" start=0x%04X len=%u", name, ext ? ext : '?', start, length);
    }
    cpu_ready(z80);
    if (jump == 0 && have)
    {
        jump = biggest_start;
    }
    if (have)
    {
        z80.PC = jump;
        log_info("disk TR-DOS jump 0x%04X", jump);
    }
}

size_t mgt_off(int track, int sector, int side)
{
    if (sector < 1)
    {
        sector = 1;
    }
    return static_cast<size_t>(((track * 2 + side) * 10 + (sector - 1))) * 512u;
}

} // namespace

Upd765::Upd765()
{
    reset();
}

void Upd765::reset()
{
    phase_ = PhaseCmd;
    cmd_need_ = 0;
    cmd_got_ = 0;
    res_len_ = 0;
    res_pos_ = 0;
    exec_.clear();
    exec_pos_ = 0;
    cyl_ = 0;
    st0_ = 0;
    interrupt_ = false;
    std::memset(cmd_, 0, sizeof(cmd_));
    std::memset(result_, 0, sizeof(result_));
}

int Upd765::command_length(uint8_t cmd) const
{
    switch (cmd & 0x1F)
    {
        case 0x03:
            return 3; /* SPECIFY */
        case 0x04:
            return 2; /* SENSE DRIVE */
        case 0x07:
            return 2; /* RECALIBRATE */
        case 0x08:
            return 1; /* SENSE INT */
        case 0x0F:
            return 3; /* SEEK */
        case 0x06:
        case 0x0C:
        case 0x05:
        case 0x09:
            return 9; /* READ/WRITE */
        case 0x0A:
            return 2; /* READ ID */
        default:
            return 1;
    }
}

void Upd765::result_st(uint8_t st0, uint8_t st1, uint8_t st2, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    result_[0] = st0;
    result_[1] = st1;
    result_[2] = st2;
    result_[3] = c;
    result_[4] = h;
    result_[5] = r;
    result_[6] = n;
    res_len_ = 7;
    res_pos_ = 0;
    phase_ = PhaseRes;
    exec_.clear();
}

void Upd765::start_command()
{
    const uint8_t op = cmd_[0] & 0x1F;
    if (op == 0x03)
    {
        phase_ = PhaseCmd;
        cmd_got_ = 0;
        return;
    }
    if (op == 0x08)
    {
        result_[0] = interrupt_ ? st0_ : 0x80;
        result_[1] = cyl_;
        res_len_ = 2;
        res_pos_ = 0;
        phase_ = PhaseRes;
        interrupt_ = false;
        cmd_got_ = 0;
        return;
    }
    if (op == 0x04)
    {
        result_[0] = 0x20; /* ready, two sided */
        res_len_ = 1;
        res_pos_ = 0;
        phase_ = PhaseRes;
        cmd_got_ = 0;
        return;
    }
    if (op == 0x07)
    {
        cyl_ = 0;
        st0_ = 0x20;
        interrupt_ = true;
        phase_ = PhaseCmd;
        cmd_got_ = 0;
        return;
    }
    if (op == 0x0F)
    {
        cyl_ = cmd_[2];
        st0_ = 0x20;
        interrupt_ = true;
        phase_ = PhaseCmd;
        cmd_got_ = 0;
        return;
    }
    if (op == 0x0A)
    {
        uint8_t c = cyl_;
        uint8_t h = static_cast<uint8_t>((cmd_[1] >> 2) & 1);
        uint8_t r = 1;
        if (disk != nullptr && !disk->empty())
        {
            for (const auto& kv : disk->sectors)
            {
                if (kv.first.c == c && kv.first.h == h)
                {
                    r = kv.first.r;
                    break;
                }
            }
        }
        result_st(0x00, 0x00, 0x00, c, h, r, 2);
        cmd_got_ = 0;
        return;
    }
    if (op == 0x06 || op == 0x0C)
    {
        const uint8_t c = cmd_[2];
        const uint8_t h = cmd_[3];
        const uint8_t r = cmd_[4];
        const uint8_t n = cmd_[5];
        size_t nlen = 0;
        const uint8_t* p = (disk != nullptr) ? disk->find(c, h, r, &nlen) : nullptr;
        if (p == nullptr || nlen == 0)
        {
            result_st(0x40, 0x04, 0x00, c, h, r, n);
            cmd_got_ = 0;
            return;
        }
        exec_.assign(p, p + nlen);
        exec_pos_ = 0;
        phase_ = PhaseExec;
        cyl_ = c;
        cmd_got_ = 0;
        return;
    }
    result_st(0x80, 0, 0, 0, 0, 0, 0);
    cmd_got_ = 0;
}

uint8_t Upd765::read_msr() const
{
    uint8_t m = 0x80; /* RQM */
    if (phase_ == PhaseRes || phase_ == PhaseExec)
    {
        m |= 0x40; /* DIO FDC→CPU */
    }
    if (phase_ == PhaseExec)
    {
        m |= 0x20; /* EXM */
    }
    if (phase_ != PhaseCmd || cmd_got_ != 0)
    {
        m |= 0x10; /* CB */
    }
    return m;
}

uint8_t Upd765::read_data()
{
    if (phase_ == PhaseExec)
    {
        if (exec_pos_ >= exec_.size())
        {
            result_st(0x00, 0x00, 0x00, cmd_[2], cmd_[3], cmd_[4], cmd_[5]);
            if (res_len_ > 0)
            {
                return result_[res_pos_++];
            }
            return 0xFF;
        }
        const uint8_t b = exec_[exec_pos_++];
        if (exec_pos_ >= exec_.size())
        {
            result_st(0x00, 0x00, 0x00, cmd_[2], cmd_[3], cmd_[4], cmd_[5]);
        }
        return b;
    }
    if (phase_ == PhaseRes)
    {
        const uint8_t b = result_[res_pos_++];
        if (res_pos_ >= res_len_)
        {
            phase_ = PhaseCmd;
            cmd_got_ = 0;
        }
        return b;
    }
    return 0xFF;
}

void Upd765::write_data(uint8_t val)
{
    if (phase_ != PhaseCmd)
    {
        return;
    }
    if (cmd_got_ == 0)
    {
        cmd_[0] = val;
        cmd_need_ = command_length(val);
        cmd_got_ = 1;
        if (cmd_need_ <= 1)
        {
            start_command();
        }
        return;
    }
    if (cmd_got_ < 9)
    {
        cmd_[cmd_got_] = val;
    }
    cmd_got_++;
    if (cmd_got_ >= cmd_need_)
    {
        start_command();
    }
}

Vg93::Vg93()
{
    reset();
}

void Vg93::reset()
{
    track = 0;
    sector = 1;
    side = 0;
    drive = 0;
    status_ = 0x04; /* track 0 */
    data_ = 0;
    sys_ = 0x3C;
    buf_.clear();
    buf_pos_ = 0;
    drq_ = false;
    intrq_ = false;
}

void Vg93::set_image(const uint8_t* data, size_t size)
{
    if (data == nullptr || size == 0)
    {
        image.clear();
        return;
    }
    image.assign(data, data + size);
}

uint8_t Vg93::read_status() const
{
    uint8_t s = status_;
    if (drq_)
    {
        s |= 0x02;
    }
    if (track == 0)
    {
        s |= 0x04;
    }
    return s;
}

uint8_t Vg93::read_system() const
{
    uint8_t v = 0x3F;
    if (drq_)
    {
        v |= 0x40;
    }
    if (intrq_)
    {
        v |= 0x80;
    }
    return v;
}

uint8_t Vg93::read_data()
{
    if (!buf_.empty() && buf_pos_ < buf_.size())
    {
        data_ = buf_[buf_pos_++];
        if (buf_pos_ >= buf_.size())
        {
            drq_ = false;
            intrq_ = true;
            status_ = (track == 0) ? 0x04 : 0x00;
        }
        else
        {
            drq_ = true;
        }
    }
    return data_;
}

void Vg93::write_data(uint8_t val)
{
    data_ = val;
}

void Vg93::write_system(uint8_t val)
{
    sys_ = val;
    drive = static_cast<uint8_t>(val & 3);
    side = static_cast<uint8_t>((val >> 4) & 1);
}

void Vg93::write_command(uint8_t val)
{
    intrq_ = false;
    drq_ = false;
    buf_.clear();
    buf_pos_ = 0;
    const uint8_t cmd = val & 0xF0;
    if (cmd == 0x00)
    {
        track = 0;
        status_ = 0x04;
        intrq_ = true;
        return;
    }
    if (cmd == 0x10)
    {
        track = data_;
        status_ = (track == 0) ? 0x04 : 0x00;
        intrq_ = true;
        return;
    }
    if ((val & 0xE0) == 0x80)
    {
        const uint8_t sec = sector == 0 ? 1 : sector;
        const size_t logical = (static_cast<size_t>(track) * 2 + side) * 16u + (sec - 1u);
        const size_t off = logical * 256u;
        buf_.assign(256, 0);
        if (off < image.size())
        {
            const size_t n = std::min(static_cast<size_t>(256), image.size() - off);
            std::memcpy(buf_.data(), image.data() + off, n);
        }
        buf_pos_ = 0;
        drq_ = true;
        status_ = 0x02;
        return;
    }
    if ((val & 0xF0) == 0xD0)
    {
        drq_ = false;
        intrq_ = true;
        status_ = (track == 0) ? 0x04 : 0x00;
        return;
    }
    intrq_ = true;
}

void disk_attach_trd(ULA& ula, const uint8_t* data, size_t size)
{
    ula.beta.set_image(data, size);
}

bool disk_load_dsk(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    DiskMap map;
    if (!parse_edsk(data, size, map))
    {
        log_error("DSK: not a CPC/+3 disk image");
        return false;
    }
    ula.edsk = map;
    ula.fdc.disk = &ula.edsk;
    ula.setModel128(true);

    std::vector<uint8_t> ids;
    for (const auto& kv : map.sectors)
    {
        if (kv.first.c == 0 && kv.first.h == 0)
        {
            ids.push_back(kv.first.r);
        }
    }
    std::sort(ids.begin(), ids.end());
    int dir_track = 0;
    int reserved = 0;
    if (!ids.empty() && ids[0] >= 0x41 && ids[0] < 0xC1)
    {
        dir_track = 2;
        reserved = 2;
    }
    std::vector<CpmFile> files;
    gather_cpm(map, dir_track, files);
    if (files.empty() && dir_track != 0)
    {
        gather_cpm(map, 0, files);
        reserved = 0;
    }
    const auto lin = linear_512(map, reserved);
    uint16_t jump = 0;
    bool have = false;
    for (const CpmFile& f : files)
    {
        const std::vector<uint8_t> raw = cpm_bytes(f, lin, 1024);
        log_info("DSK file \"%s\" bytes=%zu", f.name, raw.size());
        if (!plus3_inject(raw, z80, ula, jump, have) && raw.size() >= 2)
        {
            /* Non-PLUS3DOS binaries: leave on the FDC for +3DOS. */
        }
    }
    if (!have)
    {
        scan_plus3_raw(map, z80, ula, jump, have);
    }
    cpu_ready(z80);
    if (have)
    {
        z80.PC = jump;
        log_info("DSK PLUS3DOS CODE jump 0x%04X", jump);
    }
    else
    {
        z80.PC = 0x0000;
        log_info("DSK mounted (%zu sectors); CODE inject missed — needs +3 ROM/FDC",
                 map.sectors.size());
    }
    return true;
}

bool disk_load_mgt(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 256)
    {
        log_error("MGT: too small");
        return false;
    }
    ula.setModel128(true);
    uint16_t jump = 0;
    bool have = false;
    int seen = 0;
    for (int i = 0; i < 80; i++)
    {
        const size_t eoff = static_cast<size_t>(i) * 256;
        if (eoff + 256 > size)
        {
            break;
        }
        const uint8_t* e = data + eoff;
        const uint8_t typ = static_cast<uint8_t>(e[0] & 0x3F);
        if (typ == 0)
        {
            continue;
        }
        char name[11];
        std::memcpy(name, e + 1, 10);
        name[10] = 0;
        bool named = false;
        for (int k = 0; k < 10; k++)
        {
            const unsigned char c = static_cast<unsigned char>(name[k]);
            if (c >= 32 && c < 127 && c != ' ')
            {
                named = true;
                break;
            }
        }
        if (!named)
        {
            continue;
        }
        uint16_t nsec = static_cast<uint16_t>((e[11] << 8) | e[12]);
        if (nsec == 0)
        {
            nsec = 1;
        }
        const uint8_t trk = e[13];
        const uint8_t sec = e[14] == 0 ? 1 : e[14];
        uint8_t zxtype = e[210];
        uint16_t zxlen = static_cast<uint16_t>(e[211] | (e[212] << 8));
        uint16_t zxstart = static_cast<uint16_t>(e[213] | (e[214] << 8));
        if (zxlen == 0 || zxlen > 0xC000)
        {
            zxlen = static_cast<uint16_t>(e[212] | (e[213] << 8));
            zxstart = static_cast<uint16_t>(e[214] | (e[215] << 8));
            zxtype = e[211];
        }
        size_t off = mgt_off(trk, sec, 0);
        if (off >= size)
        {
            off = mgt_off(trk, sec, 1);
        }
        size_t nbytes = static_cast<size_t>(nsec) * 512u;
        if (off >= size)
        {
            continue;
        }
        if (off + nbytes > size)
        {
            nbytes = size - off;
        }
        const uint8_t* body = data + off;
        log_info("MGT type=%u name=\"%s\" trk=%u sec=%u start=0x%04X len=%u",
                 typ, name, trk, sec, zxstart, zxlen);
        seen++;
        if (typ == 4 || typ == 7 || zxtype == 3)
        {
            const uint16_t dest = zxstart != 0 ? zxstart : 0x8000;
            const size_t n = zxlen != 0 ? std::min(static_cast<size_t>(zxlen), nbytes) : nbytes;
            inject(ula, dest, body, n);
            jump = dest;
            have = true;
        }
        else if (typ == 5 || typ == 9)
        {
            const size_t ram_n = std::min(nbytes, static_cast<size_t>(49152));
            inject(ula, 0x4000, body, ram_n);
            jump = zxstart != 0 ? zxstart : 0x5D00;
            have = true;
            if (typ == 9)
            {
                ula.setModel128(true);
            }
        }
        else if (typ == 1 || zxtype == 0)
        {
            const size_t n = zxlen != 0 ? std::min(static_cast<size_t>(zxlen), nbytes) : nbytes;
            inject(ula, 0x5CCB, body, n);
        }
        else if (typ == 11)
        {
            const uint16_t dest = zxstart != 0 ? zxstart : 0x8000;
            inject(ula, dest, body, std::min(static_cast<size_t>(zxlen ? zxlen : nbytes), nbytes));
            jump = dest;
            have = true;
        }
    }
    cpu_ready(z80);
    if (have)
    {
        z80.PC = jump;
        log_info("MGT jump 0x%04X", jump);
    }
    else
    {
        z80.PC = 0x0000;
        log_warn("MGT: %d dirents, no CODE/snapshot to jump (needs G+DOS)", seen);
    }
    return seen > 0 || size >= 819200;
}

bool disk_load_mdr(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 543)
    {
        log_error("MDR: too small");
        return false;
    }
    const int nsec = static_cast<int>((size - 1) / 543);
    std::map<std::string, std::vector<uint8_t>> recs;
    for (int i = 0; i < nsec; i++)
    {
        const uint8_t* s = data + static_cast<size_t>(i) * 543;
        char nam[11];
        std::memcpy(nam, s + 19, 10);
        nam[10] = 0;
        const uint16_t reclen = static_cast<uint16_t>(s[17] | (s[18] << 8));
        const size_t n = std::min(static_cast<size_t>(512), static_cast<size_t>(reclen ? reclen : 512));
        recs[nam].insert(recs[nam].end(), s + 30, s + 30 + n);
    }
    uint16_t jump = 0;
    bool have = false;
    for (const auto& kv : recs)
    {
        const auto& body = kv.second;
        log_info("MDR record \"%s\" bytes=%zu", kv.first.c_str(), body.size());
        bool as_header = false;
        if (body.size() >= 17 && body[0] <= 3)
        {
            const uint16_t len = static_cast<uint16_t>(body[11] | (body[12] << 8));
            if (len > 0 && static_cast<size_t>(len) + 17 <= body.size() && len <= 0xC000)
            {
                as_header = true;
                const uint8_t type = body[0];
                const uint16_t start = static_cast<uint16_t>(body[13] | (body[14] << 8));
                const uint8_t* payload = body.data() + 17;
                const size_t n = std::min(static_cast<size_t>(len), body.size() - 17);
                if (type == 3)
                {
                    inject(ula, start, payload, n);
                    jump = start;
                    have = true;
                }
                else if (type == 0)
                {
                    inject(ula, 0x5CCB, payload, n);
                }
            }
        }
        if (!as_header && body.size() >= 2)
        {
            inject(ula, 0x8000, body.data(), std::min(body.size(), static_cast<size_t>(0x7FFF)));
            jump = 0x8000;
            have = true;
        }
    }
    cpu_ready(z80);
    if (have)
    {
        z80.PC = jump;
    }
    else
    {
        z80.PC = 0x0000;
        log_warn("MDR: no CODE record (needs Interface 1)");
    }
    return !recs.empty();
}

bool disk_load_fdi(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 14 || memcmp(data, "FDI", 3) != 0)
    {
        log_error("FDI: bad magic");
        return false;
    }
    const uint16_t cyls = static_cast<uint16_t>(data[4] | (data[5] << 8));
    const uint16_t heads = static_cast<uint16_t>(data[6] | (data[7] << 8));
    const uint16_t data_off = static_cast<uint16_t>(data[0x0A] | (data[0x0B] << 8));
    const uint16_t extra = static_cast<uint16_t>(data[0x0C] | (data[0x0D] << 8));
    size_t th = 0x0E + extra;
    std::vector<uint8_t> trd(static_cast<size_t>(std::max<uint16_t>(cyls, 1)) * std::max<uint16_t>(heads, 1) * 16u * 256u, 0);
    for (uint16_t c = 0; c < cyls; c++)
    {
        for (uint16_t h = 0; h < heads; h++)
        {
            if (th + 7 > size)
            {
                break;
            }
            const uint32_t toff = static_cast<uint32_t>(data[th] | (data[th + 1] << 8) |
                                                        (data[th + 2] << 16) | (data[th + 3] << 24));
            const uint8_t nsec = data[th + 6];
            th += 7;
            for (uint8_t s = 0; s < nsec; s++)
            {
                if (th + 7 > size)
                {
                    break;
                }
                const uint8_t C = data[th];
                const uint8_t H = data[th + 1];
                const uint8_t R = data[th + 2];
                const uint8_t N = data[th + 3];
                const uint16_t soff = static_cast<uint16_t>(data[th + 5] | (data[th + 6] << 8));
                th += 7;
                const size_t slen = 128u << (N & 7);
                const size_t pos = static_cast<size_t>(data_off) + toff + soff;
                if (pos + slen > size || slen != 256 || R == 0)
                {
                    continue;
                }
                const size_t dest = (static_cast<size_t>(C) * 16u + (R - 1u)) * 256u;
                if (dest + 256 <= trd.size())
                {
                    std::memcpy(trd.data() + dest, data + pos, 256);
                }
                (void)H;
            }
        }
    }
    ula.setModel128(true);
    disk_attach_trd(ula, trd.data(), trd.size());
    trdos_from_image(trd.data(), trd.size(), z80, ula);
    if (z80.PC == 0)
    {
        /* UKV vs Fuse sector-id order: try C,H,size,R. */
        std::fill(trd.begin(), trd.end(), 0);
        th = 0x0E + extra;
        for (uint16_t c = 0; c < cyls; c++)
        {
            for (uint16_t h = 0; h < heads; h++)
            {
                if (th + 7 > size)
                {
                    break;
                }
                const uint32_t toff = static_cast<uint32_t>(data[th] | (data[th + 1] << 8) |
                                                            (data[th + 2] << 16) | (data[th + 3] << 24));
                const uint8_t nsec = data[th + 6];
                th += 7;
                for (uint8_t s = 0; s < nsec; s++)
                {
                    if (th + 7 > size)
                    {
                        break;
                    }
                    const uint8_t C = data[th];
                    const uint8_t N = data[th + 2];
                    const uint8_t R = data[th + 3];
                    const uint16_t soff = static_cast<uint16_t>(data[th + 5] | (data[th + 6] << 8));
                    th += 7;
                    const size_t slen = 128u << (N & 7);
                    const size_t pos = static_cast<size_t>(data_off) + toff + soff;
                    if (pos + 256 > size || slen != 256 || R == 0)
                    {
                        continue;
                    }
                    const size_t dest = (static_cast<size_t>(C) * 16u + (R - 1u)) * 256u;
                    if (dest + 256 <= trd.size())
                    {
                        std::memcpy(trd.data() + dest, data + pos, 256);
                    }
                }
            }
        }
        disk_attach_trd(ula, trd.data(), trd.size());
        trdos_from_image(trd.data(), trd.size(), z80, ula);
    }
    if (z80.PC == 0)
    {
        log_warn("FDI: catalog inject missed — try .scl/.trd or --trdos-rom");
    }
    return true;
}

bool disk_load_udi(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 16 || memcmp(data, "UDI!", 4) != 0)
    {
        log_error("UDI: bad magic");
        return false;
    }
    /* v1.0 uncompressed tracks start after a 16-byte header; compressed images
     * need an MFM decoder we do not ship. Try a TR-DOS catalog scan. */
    for (size_t off = 16; off + 2048 < size; off += 256)
    {
        if (data[off] != 0 && data[off] != 0x01 && data[off] < 0x20)
        {
            continue;
        }
        /* Heuristic: 8 ASCII chars + 'C'/'B' at +8. */
        bool ascii = true;
        for (int k = 0; k < 8; k++)
        {
            const uint8_t c = data[off + k];
            if (c != 0x01 && c != 0 && (c < 0x20 || c > 0x7E))
            {
                ascii = false;
                break;
            }
        }
        const char ext = static_cast<char>(data[off + 8]);
        if (ascii && (ext == 'C' || ext == 'B' || ext == 'D'))
        {
            ula.setModel128(true);
            disk_attach_trd(ula, data + off, size - off);
            trdos_from_image(data + off, size - off, z80, ula);
            if (z80.PC != 0)
            {
                return true;
            }
        }
    }
    log_warn("UDI: compressed or non-TR-DOS image — not loaded (%zu bytes)", size);
    (void)z80;
    (void)ula;
    return false;
}

bool disk_load_d80(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 2048)
    {
        log_error("D80/D40: too small");
        return false;
    }
    ula.setModel128(true);
    disk_attach_trd(ula, data, size);
    trdos_from_image(data, size, z80, ula);
    if (z80.PC != 0)
    {
        return true;
    }
    /* Didaktik MDOS: 256-byte dirents at the start, similar to MGT. */
    return disk_load_mgt(data, size, z80, ula);
}

bool disk_load_spg(const uint8_t* data, size_t size, Z80& z80, ULA& ula)
{
    if (data == nullptr || size < 128 || memcmp(data + 32, "SpectrumProg", 12) != 0)
    {
        log_error("SPG: bad header");
        return false;
    }
    const uint8_t pack = data[0x2D];
    const uint16_t pc = static_cast<uint16_t>(data[0x30] | (data[0x31] << 8));
    const uint16_t sp = static_cast<uint16_t>(data[0x32] | (data[0x33] << 8));
    ula.setModel128(true);
    cpu_ready(z80);
    z80.PC = pc;
    z80.SP = sp != 0 ? sp : 0xFF4A;
    if (pack != 0)
    {
        log_warn("SPG: MLZ-packed image not unpacked (PC=0x%04X)", pc);
        return false;
    }
    size_t off = 128;
    int bank = 0;
    while (off + 16384 <= size && bank < 8)
    {
        std::memcpy(ula.ram_banks[bank], data + off, 16384);
        off += 16384;
        bank++;
    }
    log_info("SPG unpacked pages=%d PC=0x%04X SP=0x%04X", bank, z80.PC, z80.SP);
    return bank > 0;
}
