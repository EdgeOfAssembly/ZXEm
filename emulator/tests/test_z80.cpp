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

TEST_CASE("memRead adds 6 T contention in the first pixel slot")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    ula.line = 64;
    ula.line_tstates = ULA::ULA_FIRST_PIXEL;
    z.tstates = 0;
    (void)z.memRead(0x4000);
    REQUIRE(z.tstates == 6);
    z.tstates = 0;
    (void)z.memRead(0x0000);
    REQUIRE(z.tstates == 0);
    z.tstates = 0;
    (void)z.ioRead(0xFE);
    REQUIRE(z.tstates == 6);
    z.tstates = 0;
    (void)z.ioRead(0xFF);
    REQUIRE(z.tstates == 0);
    z.setHL(0x4000);
    ula.rom[0] = 0x7E; /* LD A,(HL) */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t == 13); /* 7 T + 6 wait on (HL) */
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
                if (z80.can_take_irq())
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

TEST_CASE("DDCB RLC (IX+d) copies result to A")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.IX = 0x8000;
    z.A = 0x00;
    ula.write(0x8001, 0x80);
    ula.rom[0] = 0xDD;
    ula.rom[1] = 0xCB;
    ula.rom[2] = 0x01; /* d */
    ula.rom[3] = 0x07; /* RLC (IX+d) → A */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t == 23);
    REQUIRE(ula.read(0x8001) == 0x01);
    REQUIRE(z.A == 0x01);
    REQUIRE((z.F & 0x01) == 0x01);
}

TEST_CASE("DDCB SET 0,(IX+d) copies result to A")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.IX = 0x8000;
    z.A = 0x00;
    z.F = 0x00;
    ula.write(0x8001, 0xF0);
    ula.rom[0] = 0xDD;
    ula.rom[1] = 0xCB;
    ula.rom[2] = 0x01;
    ula.rom[3] = 0xC7; /* SET 0,(IX+d) → A */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t == 23);
    REQUIRE(ula.read(0x8001) == 0xF1);
    REQUIRE(z.A == 0xF1);
    REQUIRE(z.F == 0x00); /* SET does not update flags */
}

TEST_CASE("DDCB BIT (IX+d) is 20 T and does not write back")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.IX = 0x8000;
    z.A = 0xAA;
    ula.write(0x8001, 0x01);
    ula.rom[0] = 0xDD;
    ula.rom[1] = 0xCB;
    ula.rom[2] = 0x01;
    ula.rom[3] = 0x46; /* BIT 0,(IX+d) */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t == 20);
    REQUIRE(z.tstates == 20);
    REQUIRE(ula.read(0x8001) == 0x01);
    REQUIRE(z.A == 0xAA);
}

TEST_CASE("DDCB BIT X/Y come from address high byte")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    /* 0xA828 high byte 0xA8 → X/Y = 0x28; operand 0x00 has neither bit. */
    z.IX = 0xA800;
    z.F = 0x01;
    ula.write(0xA828, 0x00);
    ula.rom[0] = 0xDD;
    ula.rom[1] = 0xCB;
    ula.rom[2] = 0x28;
    ula.rom[3] = 0x46; /* BIT 0,(IX+d) */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t == 20);
    REQUIRE((z.F & 0x28) == 0x28);
    REQUIRE((z.F & 0x01) == 0x01);
    REQUIRE((z.F & 0x40) != 0);
}

TEST_CASE("FDCB RLC (IY+d) copies result to A")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.IY = 0x8000;
    z.A = 0xFF;
    ula.write(0x8001, 0x80);
    ula.rom[0] = 0xFD;
    ula.rom[1] = 0xCB;
    ula.rom[2] = 0x01;
    ula.rom[3] = 0x07; /* RLC (IY+d) → A */
    z.PC = 0;
    const int t = z.execute();
    REQUIRE(t == 23);
    REQUIRE(ula.read(0x8001) == 0x01);
    REQUIRE(z.A == 0x01);
}

TEST_CASE("CB opcode increments R by 2")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.R = 0x00;
    ula.rom[0] = 0xCB;
    ula.rom[1] = 0x00; /* RLC B */
    z.PC = 0;
    z.execute();
    REQUIRE(z.R == 2);
}

TEST_CASE("HALT increments R each phantom M1")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.R = 0x80; /* bit 7 sticky */
    ula.rom[0] = 0x76; /* HALT */
    z.PC = 0;
    z.execute();
    REQUIRE(z.R == 0x81);
    REQUIRE(z.halted);
    z.execute();
    REQUIRE(z.R == 0x82);
    z.execute();
    REQUIRE(z.R == 0x83);
}

TEST_CASE("EI defers IRQ until after the guard instruction")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    ula.rom[0] = 0xFB; /* EI */
    ula.rom[1] = 0x00; /* NOP guard */
    ula.rom[2] = 0x00; /* NOP */
    z.PC = 0;
    z.execute();
    REQUIRE(z.IFF1);
    REQUIRE(z.IFF2);
    REQUIRE_FALSE(z.can_take_irq());
    z.execute(); /* guard */
    REQUIRE(z.IFF1);
    REQUIRE(z.can_take_irq());
}

TEST_CASE("INI updates H/PV/X/Y not only Z/N")
{
    ULA ula;
    Z80 z;
    z.ula = &ula;
    ula.reset();
    z.reset();
    z.setHL(0x8000);
    z.B = 0x29;
    z.C = 0xFE;
    z.F = 0x00;
    ula.rom[0] = 0xED;
    ula.rom[1] = 0xA2; /* INI */
    z.PC = 0;
    const uint8_t f0 = z.F;
    z.execute();
    REQUIRE(z.B == 0x28);
    REQUIRE(z.getHL() == 0x8001);
    REQUIRE(z.F != f0);
    REQUIRE((z.F & 0x02) != 0); /* N */
    /* Old path left F=0x02 (N only). X/Y come from B after DEC. */
    REQUIRE(z.F != 0x02);
    REQUIRE((z.F & 0x28) == 0x28);
}
