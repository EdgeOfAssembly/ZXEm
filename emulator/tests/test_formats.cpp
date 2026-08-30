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
