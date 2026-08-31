#include "media.h"
#include "snapshot.h"
#include "ula.h"
#include "vfs.h"
#include "z80.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<uint8_t> make_tap_code()
{
    /* Header: type 3 CODE "TEST" start 0x8000 length 1 */
    std::vector<uint8_t> tap;
    auto push16 = [&](uint16_t v) {
        tap.push_back(static_cast<uint8_t>(v & 0xFF));
        tap.push_back(static_cast<uint8_t>(v >> 8));
    };
    /* header block: flag + 17 + checksum = 19 */
    push16(19);
    tap.push_back(0x00);
    tap.push_back(3);
    const char name[10] = {'T', 'E', 'S', 'T', ' ', ' ', ' ', ' ', ' ', ' '};
    tap.insert(tap.end(), name, name + 10);
    push16(1);      /* length */
    push16(0x8000); /* start */
    push16(0x8000);
    tap.push_back(0x00); /* checksum dummy */
    /* data block: flag + 1 byte + checksum = 3 */
    push16(3);
    tap.push_back(0xFF);
    tap.push_back(0xC9); /* RET */
    tap.push_back(0x00);
    return tap;
}

TEST_CASE("TAP CODE block jumps to start address")
{
    auto tap = make_tap_code();
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(load_tap(tap.data(), tap.size(), z80, ula));
    REQUIRE(z80.PC == 0x8000);
    REQUIRE(ula.read(0x8000) == 0xC9);
}

TEST_CASE("z80_page_to_bank maps 3..10 to 0..7")
{
    REQUIRE(z80_page_to_bank(3, true) == 0);
    REQUIRE(z80_page_to_bank(8, true) == 5);
    REQUIRE(z80_page_to_bank(10, true) == 7);
    REQUIRE(z80_page_to_bank(2, true) == -1);
}

TEST_CASE("48K Z80 pages 8/4/5 are banks 5/2/0")
{
    REQUIRE(z80_page_to_bank(8, false) == 5);
    REQUIRE(z80_page_to_bank(4, false) == 2);
    REQUIRE(z80_page_to_bank(5, false) == 0);
    REQUIRE(z80_page_to_bank(4, true) == 1);
}

TEST_CASE("media_detect uses magic for TZX and SCL")
{
    VfsBlob b;
    b.name = "x.bin";
    b.data.assign({'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 0});
    REQUIRE(media_detect(b) == "tzx");
    b.data.assign({'S', 'I', 'N', 'C', 'L', 'A', 'I', 'R', 0});
    REQUIRE(media_detect(b) == "scl");
}

TEST_CASE("SCL dirent loads CODE at start address")
{
    /* SINCLAIR + 1 file "CODE    C" start 0x8000 len 1 sec 1, body 256 bytes. */
    std::vector<uint8_t> scl;
    const char mag[] = "SINCLAIR";
    scl.insert(scl.end(), mag, mag + 8);
    scl.push_back(1);
    const char name[8] = {'C', 'O', 'D', 'E', ' ', ' ', ' ', ' '};
    scl.insert(scl.end(), name, name + 8);
    scl.push_back('C');
    scl.push_back(0x00);
    scl.push_back(0x80); /* start 0x8000 */
    scl.push_back(0x01);
    scl.push_back(0x00); /* length 1 */
    scl.push_back(1);    /* 1 sector */
    scl.push_back(0xC9); /* RET */
    scl.resize(9 + 14 + 256, 0);
    VfsBlob b;
    b.name = "t.scl";
    b.data = std::move(scl);
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(media_load(b, z80, ula));
    REQUIRE(z80.PC == 0x8000);
    REQUIRE(ula.read(0x8000) == 0xC9);
}

TEST_CASE("POK M line pokes 48K RAM")
{
    VfsBlob b;
    const char* text = "Ntest\nM 8 32768 171 0\nY\n";
    b.data.assign(text, text + std::strlen(text));
    ULA ula;
    ula.reset();
    REQUIRE(media_apply_pok(b, ula));
    REQUIRE(ula.read(0x8000) == 171);
}

static std::vector<uint8_t> make_plus3_edsk()
{
    std::vector<uint8_t> d(256 + 768, 0);
    std::memcpy(d.data(), "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
    d[0x30] = 1;
    d[0x31] = 1;
    d[0x34] = 3;
    uint8_t* trk = d.data() + 256;
    std::memcpy(trk, "Track-Info\r\n", 12);
    trk[0x10] = 0;
    trk[0x11] = 0;
    trk[0x14] = 2;
    trk[0x15] = 1;
    trk[0x18] = 0;
    trk[0x19] = 0;
    trk[0x1A] = 1;
    trk[0x1B] = 2;
    trk[0x1E] = 0x00;
    trk[0x1F] = 0x02;
    uint8_t* sec = trk + 256;
    std::memcpy(sec, "PLUS3DOS", 8);
    sec[8] = 0x1A;
    sec[9] = 1;
    const uint32_t flen = 129;
    sec[11] = static_cast<uint8_t>(flen);
    sec[12] = static_cast<uint8_t>(flen >> 8);
    sec[15] = 3;
    sec[16] = 1;
    sec[17] = 0;
    sec[18] = 0x00;
    sec[19] = 0x80;
    sec[128] = 0xC9;
    return d;
}

TEST_CASE("EDSK PLUS3DOS CODE injects and jumps")
{
    auto dsk = make_plus3_edsk();
    VfsBlob b;
    b.name = "t.dsk";
    b.data = std::move(dsk);
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(media_load(b, z80, ula));
    REQUIRE(z80.PC == 0x8000);
    REQUIRE(ula.read(0x8000) == 0xC9);
}

TEST_CASE("MGT type 4 CODE injects")
{
    std::vector<uint8_t> mgt(21 * 512, 0);
    uint8_t* e = mgt.data();
    e[0] = 4;
    std::memcpy(e + 1, "CODE      ", 10);
    e[11] = 0;
    e[12] = 1;
    e[13] = 1;
    e[14] = 1;
    e[210] = 3;
    e[211] = 1;
    e[212] = 0;
    e[213] = 0x00;
    e[214] = 0x80;
    mgt[20 * 512] = 0xC9; /* track 1 side 0 sector 1 */
    VfsBlob b;
    b.name = "t.mgt";
    b.data = std::move(mgt);
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(media_load(b, z80, ula));
    REQUIRE(z80.PC == 0x8000);
    REQUIRE(ula.read(0x8000) == 0xC9);
}

TEST_CASE("SPG unpacked maps a page and PC")
{
    std::vector<uint8_t> spg(128 + 16384, 0);
    std::memcpy(spg.data() + 32, "SpectrumProg", 12);
    spg[0x2C] = 0x10;
    spg[0x2D] = 0;
    spg[0x30] = 0x00;
    spg[0x31] = 0xC0;
    spg[0x32] = 0x00;
    spg[0x33] = 0x60;
    spg[128] = 0xC9;
    VfsBlob b;
    b.name = "t.spg";
    b.data = std::move(spg);
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(media_load(b, z80, ula));
    REQUIRE(z80.PC == 0xC000);
    REQUIRE(ula.read(0xC000) == 0xC9);
}

TEST_CASE("Knight Lore SNA starts when key 0 is held")
{
    VfsBlob blob;
    const char* path =
        "/mnt/games/Knight Lore/Knight Lore (1984)(Ultimate Play The Game).sna";
    if (!vfs_read(path, blob))
    {
        SKIP("Knight Lore SNA not mounted");
    }
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    (void)load_rom_file("/usr/share/fuse/48.rom", ula);
    REQUIRE(media_load(blob, z80, ula));

    auto run_frames = [&](int frames) {
        for (int f = 0; f < frames; f++)
        {
            int ts = 0;
            while (ts < 69888)
            {
                const int t = z80.execute();
                ula.step(t);
                ts += t;
                if (ula.take_frame_irq() && z80.IFF1)
                {
                    z80.IFF1 = z80.IFF2 = false;
                    z80.halted = false;
                    z80.SP = static_cast<uint16_t>(z80.SP - 2);
                    ula.write(z80.SP, static_cast<uint8_t>(z80.PC & 0xFF));
                    ula.write(static_cast<uint16_t>(z80.SP + 1),
                              static_cast<uint8_t>(z80.PC >> 8));
                    z80.PC = (z80.IM == 1) ? 0x0038 : z80.PC;
                    ts += 7;
                }
            }
        }
    };

    run_frames(20);
    const uint8_t before = ula.read(0x5BA0);
    ula.setKey(4, 0, true);
    /* One IN A,(0xFE) with A=0xEF must see bit 0 clear. */
    REQUIRE((ula.ioRead(0xEFFE) & 0x01) == 0);
    run_frames(8);
    const uint8_t held = ula.read(0x5BA0);
    const uint16_t pc_held = z80.PC;
    run_frames(40);
    const uint8_t after = ula.read(0x5BA0);
    /* 5BA0 increments every menu pass while 0 is up; RET NZ on 0 stops that. */
    REQUIRE(before > 0);
    REQUIRE(after == held);
    REQUIRE((pc_held < 0xBD20 || pc_held > 0xBEB2));
}

TEST_CASE("IPF is detected but not loaded")
{
    VfsBlob b;
    b.name = "t.ipf";
    b.data.assign(16, 0);
    std::memcpy(b.data.data(), "CAPS", 4);
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(media_detect(b) == "ipf");
    REQUIRE_FALSE(media_load(b, z80, ula));
}

TEST_CASE("64K ROM fills four banks; 1FFD selects ROM 2")
{
    std::vector<uint8_t> rom(65536, 0);
    rom[0] = 0xAA;
    rom[16384] = 0xBB;
    rom[32768] = 0xCC;
    rom[49152] = 0xDD;
    ULA ula;
    ula.reset();
    REQUIRE(load_rom_blob(rom.data(), rom.size(), ula));
    REQUIRE(ula.plus3);
    REQUIRE(ula.read(0) == 0xAA);
    ula.ioWrite(0x7FFD, 0x10);
    REQUIRE(ula.read(0) == 0xBB);
    ula.ioWrite(0x7FFD, 0x00);
    ula.ioWrite(0x1FFD, 0x04);
    REQUIRE(ula.rom_index() == 2);
    REQUIRE(ula.read(0) == 0xCC);
}

TEST_CASE("TR-DOS paging steals port 1F from Kempston")
{
    ULA ula;
    ula.reset();
    ula.trdos_present = true;
    ula.setKempston(0x15);
    REQUIRE(ula.ioRead(0x1F) == 0x15);
    ula.m1_notify(0x3D00);
    REQUIRE(ula.trdos_paged);
    const uint8_t st = ula.ioRead(0x1F);
    REQUIRE(st != 0x15);
    ula.m1_notify(0x8000);
    REQUIRE_FALSE(ula.trdos_paged);
    REQUIRE(ula.ioRead(0x1F) == 0x15);
}

TEST_CASE("tape EAR bit 6 changes after TAP pulses")
{
    auto tap = make_tap_code();
    ULA ula;
    ula.reset();
    REQUIRE(ula.tape.load_tap(tap.data(), tap.size()));
    const bool first = ula.tape.ear_high();
    ula.tape.step(2168);
    REQUIRE(ula.tape.ear_high() != first);
}

TEST_CASE("uPD765 READ DATA returns mounted EDSK sector")
{
    auto dsk = make_plus3_edsk();
    VfsBlob b;
    b.name = "t.dsk";
    b.data = std::move(dsk);
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    ula.setPlus3(true);
    REQUIRE(media_load(b, z80, ula));
    ula.fdc.write_data(0x06);
    ula.fdc.write_data(0x00);
    ula.fdc.write_data(0x00);
    ula.fdc.write_data(0x00);
    ula.fdc.write_data(0x01);
    ula.fdc.write_data(0x02);
    ula.fdc.write_data(0x01);
    ula.fdc.write_data(0x2A);
    ula.fdc.write_data(0xFF);
    REQUIRE((ula.fdc.read_msr() & 0x20) != 0);
    REQUIRE(ula.fdc.read_data() == static_cast<uint8_t>('P'));
}

static uint8_t z80_ram_pattern(uint32_t i)
{
    const uint8_t p = static_cast<uint8_t>(0xA5u ^ (i * 131u) ^ (i >> 8));
    return (p == 0xED) ? static_cast<uint8_t>(0xEC) : p;
}

static void fill_48k_pattern(ULA& ula)
{
    for (uint32_t i = 0; i < 49152; i++)
    {
        ula.write(static_cast<uint16_t>(0x4000u + i), z80_ram_pattern(i));
    }
}

static bool slurp_file(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path, "rb");
    if (f == nullptr)
    {
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    const long n = ftell(f);
    if (n < 30)
    {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(n));
    const size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

static bool ram48_equal(const ULA& a, const ULA& b)
{
    return std::memcmp(a.ram_banks[5], b.ram_banks[5], 16384) == 0 &&
           std::memcmp(a.ram_banks[2], b.ram_banks[2], 16384) == 0 &&
           std::memcmp(a.ram_banks[0], b.ram_banks[0], 16384) == 0;
}

static bool payload_has(const std::vector<uint8_t>& blob,
                        uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    if (blob.size() < 34)
    {
        return false;
    }
    for (size_t i = 30; i + 3 < blob.size(); i++)
    {
        if (blob[i] == a && blob[i + 1] == b && blob[i + 2] == c && blob[i + 3] == d)
        {
            return true;
        }
    }
    return false;
}

TEST_CASE("Z80 v1 round-trip preserves a lone 0xED")
{
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    z80.reset();
    z80.PC = 0x8000;
    z80.SP = 0xFFFB;
    fill_48k_pattern(ula);
    const uint16_t lone_addr = 0x4000;
    ula.write(lone_addr, 0xED);
    for (int i = 1; i <= 8; i++)
    {
        ula.write(static_cast<uint16_t>(lone_addr + i), 0x00);
    }

    const char* path = "/tmp/zxem-med1-lone-ed.z80";
    REQUIRE(save_z80(path, z80, ula));

    std::vector<uint8_t> blob;
    REQUIRE(slurp_file(path, blob));
    REQUIRE(blob.size() >= 36);
    REQUIRE((blob[12] & 0x20) != 0);
    REQUIRE(blob[30] == 0xED);
    REQUIRE(blob[31] == 0x00);
    REQUIRE(blob[32] == 0xED);
    REQUIRE(blob[33] == 0xED);
    REQUIRE(blob[34] == 0x07);
    REQUIRE(blob[35] == 0x00);
    REQUIRE_FALSE(payload_has(blob, 0xED, 0xED, 0x01, 0xED));

    Z80 loaded;
    ULA ula2;
    loaded.ula = &ula2;
    ula2.reset();
    REQUIRE(load_z80(blob.data(), blob.size(), loaded, ula2));
    REQUIRE(loaded.PC == 0x8000);
    REQUIRE(ula2.read(lone_addr) == 0xED);
    REQUIRE(ram48_equal(ula, ula2));
}

TEST_CASE("Z80 v1 run of eight 0xED compresses and round-trips")
{
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    z80.reset();
    z80.PC = 0x1234;
    fill_48k_pattern(ula);
    const uint16_t run_addr = 0x4000;
    for (int i = 0; i < 8; i++)
    {
        ula.write(static_cast<uint16_t>(run_addr + i), 0xED);
    }

    const char* path = "/tmp/zxem-med1-ed-run.z80";
    REQUIRE(save_z80(path, z80, ula));

    std::vector<uint8_t> blob;
    REQUIRE(slurp_file(path, blob));
    REQUIRE(blob.size() >= 34);
    REQUIRE(blob[blob.size() - 4] == 0x00);
    REQUIRE(blob[blob.size() - 3] == 0xED);
    REQUIRE(blob[blob.size() - 2] == 0xED);
    REQUIRE(blob[blob.size() - 1] == 0x00);
    REQUIRE(blob[30] == 0xED);
    REQUIRE(blob[31] == 0xED);
    REQUIRE(blob[32] == 0x08);
    REQUIRE(blob[33] == 0xED);
    REQUIRE(payload_has(blob, 0xED, 0xED, 0x08, 0xED));

    Z80 loaded;
    ULA ula2;
    loaded.ula = &ula2;
    ula2.reset();
    REQUIRE(load_z80(blob.data(), blob.size(), loaded, ula2));
    REQUIRE(loaded.PC == 0x1234);
    for (int i = 0; i < 8; i++)
    {
        REQUIRE(ula2.read(static_cast<uint16_t>(run_addr + i)) == 0xED);
    }
    REQUIRE(ram48_equal(ula, ula2));
}

TEST_CASE("SNA CALL 0x0556 stack continues at LD-BYTES 0x056C")
{
    std::vector<uint8_t> sna(27 + 49152, 0);
    const uint16_t sp = 0xFF00;
    sna[23] = static_cast<uint8_t>(sp & 0xFF);
    sna[24] = static_cast<uint8_t>(sp >> 8);
    sna[25] = 1;
    const size_t off = static_cast<size_t>(27 + (sp - 0x4000));
    sna[off] = 0xCD;
    sna[off + 1] = 0x56;
    sna[off + 2] = 0x05;

    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(load_sna(sna.data(), sna.size(), z80, ula));
    REQUIRE(z80.PC == 0x056C);
    REQUIRE(z80.SP == static_cast<uint16_t>(sp + 2));
}

TEST_CASE("SNA 48K pops a normal stacked PC")
{
    std::vector<uint8_t> sna(27 + 49152, 0);
    const uint16_t sp = 0xFF00;
    sna[23] = static_cast<uint8_t>(sp & 0xFF);
    sna[24] = static_cast<uint8_t>(sp >> 8);
    const size_t off = static_cast<size_t>(27 + (sp - 0x4000));
    sna[off] = 0x00;
    sna[off + 1] = 0x80;

    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    REQUIRE(load_sna(sna.data(), sna.size(), z80, ula));
    REQUIRE(z80.PC == 0x8000);
    REQUIRE(z80.SP == static_cast<uint16_t>(sp + 2));
}

TEST_CASE("Z80 v3 128K save round-trips distinct banks")
{
    Z80 z80;
    ULA ula;
    z80.ula = &ula;
    ula.reset();
    z80.reset();
    ula.setModel128(true);
    z80.PC = 0xC000;
    z80.SP = 0xFFFD;
    ula.port7ffd = 0x00;

    std::memset(ula.ram_banks[5], 0x51, 16384);
    std::memset(ula.ram_banks[2], 0x22, 16384);
    std::memset(ula.ram_banks[0], 0xA0, 16384);
    std::memset(ula.ram_banks[1], 0x11, 16384);
    std::memset(ula.ram_banks[3], 0x33, 16384);
    std::memset(ula.ram_banks[4], 0x44, 16384);
    std::memset(ula.ram_banks[6], 0x66, 16384);
    std::memset(ula.ram_banks[7], 0xB7, 16384);

    const char* path = "/tmp/zxem-med7-128.z80";
    REQUIRE(save_z80(path, z80, ula));

    std::vector<uint8_t> blob;
    REQUIRE(slurp_file(path, blob));
    REQUIRE(blob.size() > 30 + 54);
    REQUIRE(blob[6] == 0);
    REQUIRE(blob[7] == 0);
    const uint16_t extra = static_cast<uint16_t>(blob[30] | (static_cast<uint16_t>(blob[31]) << 8));
    REQUIRE(extra >= 54);
    REQUIRE(blob[32] == 0x00);
    REQUIRE(blob[33] == 0xC0);
    const uint8_t hw = blob[34];
    REQUIRE((hw == 3 || hw == 4 || hw == 7));
    REQUIRE(ula.ram_banks[5][0] != ula.ram_banks[7][0]);

    Z80 loaded;
    ULA ula2;
    loaded.ula = &ula2;
    ula2.reset();
    REQUIRE(load_z80(blob.data(), blob.size(), loaded, ula2));
    REQUIRE(ula2.is128);
    REQUIRE(loaded.PC == 0xC000);
    REQUIRE(ula2.port7ffd == 0x00);
    REQUIRE(ula2.ram_banks[5][0] == 0x51);
    REQUIRE(ula2.ram_banks[7][0] == 0xB7);
    REQUIRE(ula2.ram_banks[5][0] != ula2.ram_banks[7][0]);
    REQUIRE(std::memcmp(ula.ram_banks[5], ula2.ram_banks[5], 16384) == 0);
    REQUIRE(std::memcmp(ula.ram_banks[2], ula2.ram_banks[2], 16384) == 0);
    REQUIRE(std::memcmp(ula.ram_banks[0], ula2.ram_banks[0], 16384) == 0);
    REQUIRE(std::memcmp(ula.ram_banks[7], ula2.ram_banks[7], 16384) == 0);
}
