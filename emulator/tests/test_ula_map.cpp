#include "ula.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("48K map uses banks 5/2/0")
{
    ULA ula;
    ula.setModel128(false);
    ula.reset();
    ula.write(0x4000, 0x11);
    ula.write(0x8000, 0x22);
    ula.write(0xC000, 0x33);
    REQUIRE(ula.read(0x4000) == 0x11);
    REQUIRE(ula.read(0x8000) == 0x22);
    REQUIRE(ula.read(0xC000) == 0x33);
    REQUIRE(ula.ram_banks[5][0] == 0x11);
    REQUIRE(ula.ram_banks[2][0] == 0x22);
    REQUIRE(ula.ram_banks[0][0] == 0x33);
}

TEST_CASE("128K C000 follows port 7FFD bank bits")
{
    ULA ula;
    ula.setModel128(true);
    ula.reset();
    ula.port7ffd = 0x00;
    ula.write(0xC000, 0xAA);
    REQUIRE(ula.ram_banks[0][0] == 0xAA);
    ula.port7ffd = 0x06;
    ula.write(0xC000, 0xBB);
    REQUIRE(ula.ram_banks[6][0] == 0xBB);
    ula.port7ffd = 0x00;
    REQUIRE(ula.read(0xC000) == 0xAA);
    REQUIRE(ula.read(0x8000) == 0x00); /* bank 2 is not paged */
}

TEST_CASE("bitmap address differs for scanlines in the same cell")
{
    ULA ula;
    ula.reset();
    ula.ram_banks[5][0] = 0x80;
    ula.ram_banks[5][0x0100] = 0x01;
    ula.ram_banks[5][0x1800] = 0x07; /* white ink, black paper */
    uint32_t pixels[ULA::SCREEN_WIDTH * ULA::SCREEN_HEIGHT];
    ula.renderFrame(pixels, ULA::SCREEN_WIDTH * 4);
    REQUIRE(pixels[0] != pixels[ULA::SCREEN_WIDTH]);
}

TEST_CASE("128K 8000 is always bank 2")
{
    ULA ula;
    ula.setModel128(true);
    ula.reset();
    ula.port7ffd = 0x07;
    ula.write(0x8000, 0x42);
    REQUIRE(ula.ram_banks[2][0] == 0x42);
    REQUIRE(ula.read(0x8000) == 0x42);
    REQUIRE(ula.ram_banks[7][0] != 0x42);
}
