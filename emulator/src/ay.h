#pragma once
#include <cstdint>
#include <cmath>

class AY38912 {
public:
    AY38912();
    void reset();
    void writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg) const;
    /** @brief Latch register index (port 0xFFFD). */
    void select(uint8_t reg);
    /** @brief Write the latched register (port 0xBFFD). */
    void write_data(uint8_t val);
    /** @brief Read the latched register (port 0xFFFD). */
    uint8_t read_data() const;
    uint8_t selected() const { return reg_select; }
    void step(int cycles);
    float sample() const;

    static const int CLOCK_DIV = 8;

private:
    uint8_t regs[16];
    uint8_t reg_select;
    int counter_a, counter_b, counter_c;
    int noise_counter;
    int env_counter;
    bool tone_a, tone_b, tone_c;
    bool noise_out;
    int env_volume;
    int env_period;
    int env_shape;
    int env_step;
    bool env_hold, env_alt, env_attack;
    int cycle_acc;
};
