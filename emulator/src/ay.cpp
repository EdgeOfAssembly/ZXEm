#include "ay.h"
#include <cstring>

namespace
{
/* ~2–3 dB/step AY DAC (√2 voltage). vol 0 is silent; vol 15 is full scale. */
constexpr float kAyDac[16] = {
    0.0f,   0.0078f, 0.011f, 0.015f,
    0.022f, 0.031f,  0.044f, 0.062f,
    0.088f, 0.125f,  0.177f, 0.25f,
    0.354f, 0.5f,    0.707f, 1.0f
};
}

AY38912::AY38912() { reset(); }

void AY38912::reset() {
    memset(regs, 0, sizeof(regs));
    reg_select = 0;
    counter_a = counter_b = counter_c = 0;
    noise_counter = 0;
    noise_lfsr = 1;
    env_counter = 0;
    tone_a = tone_b = tone_c = false;
    noise_out = false;
    env_volume = 0;
    env_period = 1;
    env_shape = 0;
    env_step = 0;
    env_hold = env_alt = env_attack = false;
    cycle_acc = 0;
}

void AY38912::writeReg(uint8_t reg, uint8_t val) {
    if (reg < 16) {
        regs[reg] = val;
        if (reg == 11 || reg == 12) {
            env_period = regs[11] | (static_cast<uint16_t>(regs[12]) << 8);
            if (env_period < 1) env_period = 1;
        }
        if (reg == 13) {
            env_shape = regs[13] & 0x0F;
            env_hold = (env_shape & 1) != 0;
            env_alt = (env_shape & 2) != 0;
            env_attack = (env_shape & 4) != 0;
            if ((env_shape & 8) == 0)
            {
                /* Continue=0: one ramp then hold at 0 (Hold/Alt ignored). */
                env_hold = true;
            }
            env_step = 0;
            env_volume = env_attack ? 0 : 15;
            env_period = regs[11] | (static_cast<uint16_t>(regs[12]) << 8);
            if (env_period < 1)
            {
                env_period = 1;
            }
            /* Reload so the first step is a full period, not leftover phase. */
            env_counter = env_period;
        }
    }
}

uint8_t AY38912::readReg(uint8_t reg) const
{
    /* Unused bits read as 0 (AY-3-8910). Storage is unmasked. */
    static constexpr uint8_t kReadMask[16] = {
        0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
        0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
    };
    if (reg < 16)
    {
        return static_cast<uint8_t>(regs[reg] & kReadMask[reg]);
    }
    return 0xFF;
}

void AY38912::select(uint8_t reg)
{
    reg_select = reg & 0x0F;
}

void AY38912::write_data(uint8_t val)
{
    writeReg(reg_select, val);
}

uint8_t AY38912::read_data() const
{
    return readReg(reg_select);
}

void AY38912::step(int cycles) {
    cycle_acc += cycles;
    while (cycle_acc >= CLOCK_DIV) {
        cycle_acc -= CLOCK_DIV;

        counter_a--;
        if (counter_a <= 0) {
            uint16_t period = regs[0] | ((uint16_t)(regs[1] & 0x0F) << 8);
            if (period < 1) period = 1;
            counter_a += period;
            tone_a = !tone_a;
        }

        counter_b--;
        if (counter_b <= 0) {
            uint16_t period = regs[2] | ((uint16_t)(regs[3] & 0x0F) << 8);
            if (period < 1) period = 1;
            counter_b += period;
            tone_b = !tone_b;
        }

        counter_c--;
        if (counter_c <= 0) {
            uint16_t period = regs[4] | ((uint16_t)(regs[5] & 0x0F) << 8);
            if (period < 1) period = 1;
            counter_c += period;
            tone_c = !tone_c;
        }

        noise_counter--;
        if (noise_counter <= 0) {
            uint8_t np = regs[6] & 0x1F;
            if (np < 1) np = 1;
            noise_counter += np;
            const uint32_t bit = (noise_lfsr ^ (noise_lfsr >> 3)) & 1u;
            noise_lfsr = (noise_lfsr >> 1) | (bit << 16);
            noise_out = (noise_lfsr & 1u) != 0;
        }

        env_counter--;
        if (env_counter <= 0)
        {
            env_counter += env_period;
            /* env_step != 0: frozen after Hold or Continue=0 one-shot. */
            if (env_period > 0 && env_step == 0)
            {
                if (env_attack)
                {
                    env_volume++;
                    if (env_volume >= 15)
                    {
                        env_volume = 15;
                        if (env_hold)
                        {
                            env_step = 1;
                            if ((env_shape & 8) == 0)
                            {
                                env_volume = 0;
                            }
                        }
                        else if (env_alt)
                        {
                            env_attack = false;
                            env_volume = 14;
                        }
                        else
                        {
                            env_volume = 0;
                        }
                    }
                }
                else
                {
                    env_volume--;
                    if (env_volume < 0)
                    {
                        env_volume = 0;
                        if (env_hold)
                        {
                            env_step = 1;
                        }
                        else if (env_alt)
                        {
                            env_attack = true;
                            env_volume = 1;
                        }
                        else
                        {
                            env_volume = 15;
                        }
                    }
                }
            }
        }
    }
}

float AY38912::sample() const
{
    auto chan = [&](int ch) -> float
    {
        bool tone = tone_c;
        if (ch == 0)
        {
            tone = tone_a;
        }
        else if (ch == 1)
        {
            tone = tone_b;
        }

        const uint8_t mixer = regs[7];
        const bool tone_off = (mixer >> ch) & 1;
        const bool noise_off = (mixer >> (ch + 3)) & 1;
        const bool out = (tone || tone_off) && (noise_out || noise_off);

        const uint8_t vol_reg = regs[8 + ch];
        const int vol = (vol_reg & 0x10) ? env_volume : (vol_reg & 0x0F);
        const int vol_idx = (vol < 0) ? 0 : ((vol > 15) ? 15 : vol);
        return out ? kAyDac[vol_idx] : 0.0f;
    };

    return (chan(0) + chan(1) + chan(2)) / 3.0f;
}
