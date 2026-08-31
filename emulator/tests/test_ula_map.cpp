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
    const int idx = (ULA::ULA_FIRST_LINE * ULA::SCREEN_WIDTH) + ULA::BORDER_LEFT;
    REQUIRE(pixels[idx] != pixels[idx + ULA::SCREEN_WIDTH]);
    REQUIRE(pixels[idx] == 0xFFCDCDCD); /* ink 7, bit7 set */
    REQUIRE(pixels[idx + 1] == 0xFF000000); /* paper */
}

TEST_CASE("renderFrame draws per-line border around paper")
{
    ULA ula;
    ula.reset();
    ula.ram_banks[5][0] = 0x80;
    ula.ram_banks[5][0x1800] = 0x07; /* white ink, black paper */
    ula.line = ULA::ULA_FIRST_LINE;
    ula.ioWrite(0x00FE, 0x05);
    uint32_t pixels[ULA::SCREEN_WIDTH * ULA::SCREEN_HEIGHT];
    ula.renderFrame(pixels, ULA::SCREEN_WIDTH * 4);
    const int row = ULA::ULA_FIRST_LINE * ULA::SCREEN_WIDTH;
    const int paper = row + ULA::BORDER_LEFT;
    REQUIRE(pixels[row] == 0xFF00CDCD); /* left border, colour 5, never bright */
    REQUIRE(pixels[paper - 1] == 0xFF00CDCD);
    REQUIRE(pixels[paper] == 0xFFCDCDCD); /* paper ink from bitmap */
    REQUIRE(pixels[paper + ULA::PAPER_WIDTH] == 0xFF00CDCD); /* right border */
    REQUIRE(pixels[0] == 0xFF000000); /* other lines stay border 0 */
}

TEST_CASE("isContended uses line window not integer divide")
{
    ULA ula;
    ula.reset();
    ula.line = 0;
    ula.line_tstates = 128;
    REQUIRE(ula.isContended(0x4000) == 0);
    ula.line = 64;
    ula.line_tstates = 128;
    REQUIRE(ula.isContended(0x4000) == 6);
    REQUIRE(ula.isContended(0x0000) == 0);
    REQUIRE(ula.isContended(0x8000) == 0);
    ula.line_tstates = 0;
    REQUIRE(ula.isContended(0x4000) == 0);
    ula.setModel128(true);
    ula.port7ffd = 0x01;
    ula.line = 64;
    ula.line_tstates = 128;
    REQUIRE(ula.isContended(0xC000) == 6);
    ula.port7ffd = 0x00;
    REQUIRE(ula.isContended(0xC000) == 0);
}

TEST_CASE("contention delay is 6,5,4,3,2,1,0,0 in the pixel window")
{
    ULA ula;
    ula.reset();
    ula.line = 64;
    static constexpr int kPat[8] = {6, 5, 4, 3, 2, 1, 0, 0};
    for (int i = 0; i < 8; i++)
    {
        ula.line_tstates = ULA::ULA_FIRST_PIXEL + i;
        REQUIRE(ula.isContended(0x4000) == kPat[i]);
    }
    ula.line_tstates = ULA::ULA_FIRST_PIXEL + 8;
    REQUIRE(ula.isContended(0x4000) == 6);
    ula.line_tstates = ULA::ULA_FIRST_PIXEL;
    REQUIRE(ula.io_contention(0xFE) == 6);
    REQUIRE(ula.io_contention(0xFF) == 0);
    ula.line = 63;
    REQUIRE(ula.isContended(0x4000) == 0);
    REQUIRE(ula.io_contention(0xFE) == 0);
}

TEST_CASE("48K t_frame is 69888")
{
    ULA ula;
    REQUIRE(ula.t_line() == 224);
    REQUIRE(ula.lines() == 312);
    REQUIRE(ula.t_frame() == 69888);
    REQUIRE(ula.cpu_hz() == 3500000);
    REQUIRE(ula.t_line() * ula.lines() == ula.t_frame());
}

TEST_CASE("128K t_frame is 70908")
{
    ULA ula;
    ula.setModel128(true);
    REQUIRE(ula.t_line() == 228);
    REQUIRE(ula.lines() == 311);
    REQUIRE(ula.t_frame() == 70908);
    REQUIRE(ula.cpu_hz() == 3546900);
    REQUIRE(ula.t_line() * ula.lines() == ula.t_frame());
    ula.reset();
    ula.step(ula.t_frame());
    REQUIRE(ula.line == 0);
    REQUIRE(ula.frame_tstates == 0);
    REQUIRE(ula.take_frame_irq());
    ula.setPlus3(true);
    REQUIRE(ula.t_frame() == 70908);
    ula.setModel128(false);
    REQUIRE(ula.t_frame() == 69888);
    REQUIRE(ula.cpu_hz() == 3500000);
}

TEST_CASE("key 0 clears bit 0 on port 0xEFFE")
{
    ULA ula;
    ula.reset();
    REQUIRE((ula.ioRead(0xEFFE) & 0x01) != 0);
    ula.setKey(4, 0, true);
    REQUIRE((ula.ioRead(0xEFFE) & 0x01) == 0);
    REQUIRE((ula.ioRead(0xEFFE) & 0x1E) == 0x1E);
    ula.setKey(4, 0, false);
    REQUIRE((ula.ioRead(0xEFFE) & 0x01) != 0);
}

TEST_CASE("ULA::step bulk-advances a full line and frame wrap")
{
    ULA ula;
    ula.reset();
    REQUIRE(ula.line == 0);
    REQUIRE(ula.line_tstates == 0);
    ula.step(ULA::TSTATES_PER_LINE);
    REQUIRE(ula.line == 1);
    REQUIRE(ula.line_tstates == 0);
    REQUIRE(ula.tstates == ULA::TSTATES_PER_LINE);
    ula.step(ULA::TSTATES_PER_LINE * (ULA::LINES_PER_FRAME - 1));
    REQUIRE(ula.line == 0);
    REQUIRE(ula.frame_tstates == 0);
    REQUIRE(ula.tstates == ULA::TSTATES_PER_FRAME);
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

TEST_CASE("floating bus returns display byte in the pixel window")
{
    ULA ula;
    ula.reset();
    ula.ram_banks[5][0] = 0xA5;
    ula.ram_banks[5][1] = 0x3C;
    ula.ram_banks[5][0x0100] = 0x81;
    ula.ram_banks[5][0x1800] = 0x47;
    ula.ram_banks[5][0x1801] = 0x12;

    ula.line = 0;
    ula.line_tstates = ULA::ULA_FIRST_PIXEL;
    REQUIRE(ula.ioRead(0xFF) == 0xFF);

    ula.line = 64;
    ula.line_tstates = 0;
    REQUIRE(ula.ioRead(0xFF) == 0xFF);

    ula.line_tstates = ULA::ULA_FIRST_PIXEL; /* phase 0: bitmap col 0 */
    REQUIRE(ula.ioRead(0xFF) == 0xA5);
    REQUIRE(ula.ioRead(0xFF) != 0xFF);

    ula.line_tstates = ULA::ULA_FIRST_PIXEL + 1; /* phase 1: attr col 0 */
    REQUIRE(ula.ioRead(0xFF) == 0x47);

    ula.line_tstates = ULA::ULA_FIRST_PIXEL + 2; /* phase 2: bitmap col 1 */
    REQUIRE(ula.ioRead(0xFF) == 0x3C);

    ula.line_tstates = ULA::ULA_FIRST_PIXEL + 3; /* phase 3: attr col 1 */
    REQUIRE(ula.ioRead(0xFF) == 0x12);

    ula.line_tstates = ULA::ULA_FIRST_PIXEL + 4; /* idle half of the 8 T cell */
    REQUIRE(ula.ioRead(0xFF) == 0xFF);

    ula.line = 65; /* y=1 bitmap at 0x0100 */
    ula.line_tstates = ULA::ULA_FIRST_PIXEL;
    REQUIRE(ula.ioRead(0xFF) == 0x81);

    ula.setPlus3(true);
    ula.line = 64;
    ula.line_tstates = ULA::ULA_FIRST_PIXEL;
    REQUIRE(ula.ioRead(0xFF) == 0xFF);
}

TEST_CASE("floating bus follows 128K shadow screen bank")
{
    ULA ula;
    ula.setModel128(true);
    ula.reset();
    ula.port7ffd = 0x08;
    ula.ram_banks[5][0] = 0x11;
    ula.ram_banks[7][0] = 0x99;
    ula.line = 64;
    ula.line_tstates = ULA::ULA_FIRST_PIXEL;
    REQUIRE(ula.ioRead(0xFF) == 0x99);
    ula.port7ffd = 0x00;
    REQUIRE(ula.ioRead(0xFF) == 0x11);
}

TEST_CASE("port FE write records per-line border")
{
    ULA ula;
    ula.reset();
    ula.line = 10;
    ula.ioWrite(0x00FE, 0x05);
    REQUIRE(ula.border == 5);
    REQUIRE(ula.border_line[10] == 5);
    REQUIRE(ula.border_line[11] == 0);

    ula.line_tstates = ula.t_line() - 1;
    ula.step(1);
    REQUIRE(ula.line == 11);
    REQUIRE(ula.border_line[11] == 5);

    ula.ioWrite(0xFE, 0x02);
    REQUIRE(ula.border == 2);
    REQUIRE(ula.border_line[11] == 2);
    REQUIRE(ula.border_line[10] == 5);

    ula.line = ULA::LINES_PER_FRAME - 1;
    ula.line_tstates = ula.t_line() - 1;
    ula.border = 3;
    ula.step(1);
    REQUIRE(ula.line == 0);
    REQUIRE(ula.border_line[0] == 3);
}
