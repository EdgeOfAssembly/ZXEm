#pragma once
#include <cstdint>
#include <cstring>

class ULA {
public:
    uint8_t rom[16384];
    uint8_t ram[49152];
    uint8_t border;
    bool beeper;
    int tstates;
    int frame_tstates;
    int line;
    int line_tstates;
    bool flash;
    int flash_counter;

    ULA();
    void reset();
    void resetToSyntheticROM();
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t val);
    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t val);
    void step(int cycles);
    void renderFrame(uint32_t* pixels, int pitch);
    bool isContended(uint16_t addr, int tstate);

    static const int SCREEN_WIDTH = 256;
    static const int SCREEN_HEIGHT = 192;
    static const int TSTATES_PER_LINE = 224;
    static const int LINES_PER_FRAME = 312;
    static const int TSTATES_PER_FRAME = 69888;
    static const int ULA_FIRST_PIXEL = 128;
    static const int ULA_LAST_PIXEL = 128 + 128;
    static const int ULA_FIRST_LINE = 64;
    static const int ULA_LAST_LINE = 64 + 192;

    uint8_t keyboard[8];
    uint8_t kempston;
    void setKey(int row, int bit, bool pressed);
    void setKempston(uint8_t v) { kempston = v; }

    // Audio state tracking
    uint64_t beeper_transition_tstates;
    uint64_t last_beeper_state;
    bool beeper_state;
    bool beeper_changed;
    void beeperSet(bool on);
    float currentAudioSample() const;
};
