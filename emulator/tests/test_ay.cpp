#include "ay.h"
#include "ula.h"

#include <cmath>
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

TEST_CASE("AY readReg masks unused bits")
{
    AY38912 ay;
    ay.writeReg(1, 0xFF);
    REQUIRE(ay.readReg(1) == 0x0F);
    ay.writeReg(6, 0xFF);
    REQUIRE(ay.readReg(6) == 0x1F);
    ay.writeReg(13, 0xFF);
    REQUIRE(ay.readReg(13) == 0x0F);
    ay.writeReg(0, 0xA5);
    REQUIRE(ay.readReg(0) == 0xA5);
}

TEST_CASE("AY mix amplitude exceeds beeper at full channel volume")
{
    /* Mixer 0x3F: tone+noise off → DC at each channel volume. */
    ULA ula;
    ula.ay.writeReg(7, 0x3F);
    ula.ay.writeReg(8, 15);
    ula.ay.writeReg(9, 0);
    ula.ay.writeReg(10, 0);
    ula.beeper_state = false;

    /* currentAudioSample() always adds ±kBeepAmp; subtract the low-beep offset. */
    constexpr float kBeepAmp = 0.10f;
    constexpr float kAyMix = 0.70f;
    const float ay_only = ula.currentAudioSample() - (-kBeepAmp);
    const float ay_scaled = kAyMix * ula.ay.sample();

    REQUIRE(std::fabs(ay_only - ay_scaled) < 1.0e-6f);
    /* One full-volume channel: 0.70 * (DAC/3) must exceed kBeepAmp 0.10. */
    REQUIRE(ay_scaled > kBeepAmp);
}
