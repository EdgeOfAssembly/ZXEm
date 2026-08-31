#include "ay.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
/** Tone+noise off on A/B/C → constant DC at each channel's volume. */
void dc_mixer(AY38912& ay)
{
    ay.writeReg(7, 0x3F);
    ay.writeReg(9, 0);
    ay.writeReg(10, 0);
}
}

TEST_CASE("AY volume 1 is logarithmic not linear 1/15")
{
    AY38912 ay;
    dc_mixer(ay);
    ay.writeReg(8, 1);
    const float s = ay.sample();
    const float linear_mix = (1.0f / 15.0f) / 3.0f;
    REQUIRE(s > 0.0f);
    REQUIRE(s < linear_mix * 0.25f);
}

TEST_CASE("AY envelope shape 0 (C=0 decay) holds at 0")
{
    AY38912 ay;
    dc_mixer(ay);
    ay.writeReg(8, 0x10);
    ay.writeReg(11, 1);
    ay.writeReg(12, 0);
    ay.writeReg(13, 0);
    REQUIRE(ay.sample() > 0.0f);
    ay.step(AY38912::CLOCK_DIV * 64);
    REQUIRE(ay.sample() == 0.0f);
}

TEST_CASE("AY envelope shape 4 (C=0 attack) holds at 0 not 15")
{
    AY38912 ay;
    dc_mixer(ay);
    ay.writeReg(8, 0x10);
    ay.writeReg(11, 1);
    ay.writeReg(12, 0);
    ay.writeReg(13, 4);
    REQUIRE(ay.sample() == 0.0f);
    float peak = 0.0f;
    for (int i = 0; i < 16; i++)
    {
        ay.step(AY38912::CLOCK_DIV);
        const float s = ay.sample();
        if (s > peak)
        {
            peak = s;
        }
    }
    REQUIRE(peak > 0.1f);
    ay.step(AY38912::CLOCK_DIV * 32);
    REQUIRE(ay.sample() == 0.0f);
}
