#include "ula.h"
#include "z80.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("setFlag53 preserves S/Z/C")
{
    Z80 z;
    z.F = 0xC1; /* S Z C */
    z.setFlag53(0x28);
    REQUIRE((z.F & 0xC1) == 0xC1);
    REQUIRE((z.F & 0x28) == 0x28);
}

TEST_CASE("EX AF,AF' swaps accumulators only")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.A = 0x11;
    z.F = 0x22;
    z.A_ = 0x33;
    z.F_ = 0x44;
    ula.rom[0] = 0x08; /* EX AF,AF' */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t == 4);
    REQUIRE(z.A == 0x33);
    REQUIRE(z.F == 0x44);
    REQUIRE(z.A_ == 0x11);
    REQUIRE(z.F_ == 0x22);
}

TEST_CASE("EXX does not swap AF")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.A = 0xAA;
    z.F = 0x01;
    z.setBC(0x1111);
    z.setBC_(0x2222);
    ula.rom[0] = 0xD9;
    z.PC = 0;
    z.execute();
    REQUIRE(z.A == 0xAA);
    REQUIRE(z.getBC() == 0x2222);
    REQUIRE(z.getBC_() == 0x1111);
}

TEST_CASE("DJNZ does not change flags")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.B = 2;
    z.F = 0x00;
    ula.rom[0] = 0x10;
    ula.rom[1] = 0x00;
    z.PC = 0;
    z.execute();
    REQUIRE(z.B == 1);
    REQUIRE(z.F == 0x00);
}

TEST_CASE("ED 4A is ADC HL,BC")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.setHL(0x1000);
    z.setBC(0x0001);
    z.F = 0x01; /* carry in */
    ula.rom[0] = 0xED;
    ula.rom[1] = 0x4A;
    z.PC = 0;
    z.execute();
    REQUIRE(z.getHL() == 0x1002);
}

TEST_CASE("ED 5F is LD A,R not LD R,A")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.A = 0x00;
    z.R = 0x55;
    ula.rom[0] = 0xED;
    ula.rom[1] = 0x5F;
    z.PC = 0;
    z.execute();
    REQUIRE(z.A != 0x00);
}

TEST_CASE("prefixed execute returns instruction T-states not the accumulator")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.tstates = 1000;
    ula.rom[0] = 0xED;
    ula.rom[1] = 0x44; /* NEG */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t < 32);
    REQUIRE(t > 0);
}

TEST_CASE("maskable INT fires once per frame from EI HALT")
{
    ULA ula;
    Z80 z80;
    z80.ula = &ula;
    ula.reset();
    z80.reset();
    z80.SP = 0xFFFE;
    z80.PC = 0x8000;
    z80.IM = 1;
    ula.write(0x8000, 0xFB); /* EI */
    ula.write(0x8001, 0x76); /* HALT */

    int irqs = 0;
    bool saw_rst38 = false;
    constexpr int kFrames = 3;
    constexpr int kTstatesPerFrame = 69888;
    for (int frame = 0; frame < kFrames; frame++)
    {
        int ts = 0;
        while (ts < kTstatesPerFrame)
        {
            const int t = z80.execute();
            ula.step(t);
            ts += t;
            if (ula.take_frame_irq())
            {
                if (z80.IFF1)
                {
                    z80.IFF1 = z80.IFF2 = false;
                    z80.halted = false;
                    ula.step(7);
                    z80.SP = static_cast<uint16_t>(z80.SP - 2);
                    ula.write(z80.SP, static_cast<uint8_t>(z80.PC & 0xFF));
                    ula.write(static_cast<uint16_t>(z80.SP + 1), static_cast<uint8_t>(z80.PC >> 8));
                    if (z80.IM == 2)
                    {
                        const uint16_t vec =
                            static_cast<uint16_t>((static_cast<uint16_t>(z80.I) << 8) | 0xFF);
                        const uint8_t lo = ula.read(vec);
                        const uint8_t hi = ula.read(static_cast<uint16_t>(vec + 1));
                        z80.PC = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
                    }
                    else
                    {
                        z80.PC = 0x0038;
                    }
                    ts += 7;
                    irqs++;
                    if (z80.PC == 0x0038)
                    {
                        saw_rst38 = true;
                    }
                    /* Re-arm without executing ROM at 0x0038. */
                    z80.IFF1 = z80.IFF2 = true;
                    z80.halted = true;
                }
            }
        }
    }
    REQUIRE(irqs == 3);
    REQUIRE(saw_rst38);
}
