#include "media.h"
#include "snapshot.h"
#include "ula.h"
#include "z80.h"

#include <catch2/catch_test_macros.hpp>
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
