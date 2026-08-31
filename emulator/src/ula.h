#pragma once
#include <cstdint>
#include <cstring>
#include "ay.h"
#include "disk.h"
#include "tape.h"

class ULA {
public:
    uint8_t rom[16384];
    uint8_t rom1[16384];
    uint8_t rom2[16384];
    uint8_t rom3[16384];
    uint8_t trdos_rom[16384];
    uint8_t ram[49152];
    uint8_t ram_banks[8][16384];
    uint8_t border;
    bool beeper;
    int tstates;
    int frame_tstates;
    bool frame_irq;
    int line;
    int line_tstates;
    bool flash;
    int flash_counter;

    bool is128;
    bool plus3;
    bool trdos_present;
    bool trdos_paged;
    uint8_t port7ffd;
    uint8_t port1ffd;
    AY38912 ay;
    DiskMap edsk;
    Upd765 fdc;
    Vg93 beta;
    TapeDeck tape;

    ULA();
    void reset();
    void resetToSyntheticROM();
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t val);
    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t val);
    void step(int cycles);
    /**
     * @brief Consume the pending maskable frame INT (set when the raster wraps line 312→0).
     * @return true if a frame INT was pending; the flag is cleared.
     * @note Poll this instead of @c frame_tstates — wrap zeroes the counter in the same step.
     */
    bool take_frame_irq()
    {
        const bool pending = frame_irq;
        frame_irq = false;
        return pending;
    }
    /**
     * @brief Decode the 256×192 display file into ARGB8888 (one pixel per uint32).
     * @param[out] pixels Destination; at least 256×192 entries if pitch is 256×4.
     * @param[in] pitch Bytes per destination row (typically SCREEN_WIDTH * 4).
     */
    void renderFrame(uint32_t* pixels, int pitch);
    /**
     * @brief True if this access waits on the ULA (bank 5 / odd 128K banks during pixels).
     * Uses @c line / @c line_tstates (no divide).
     */
    bool isContended(uint16_t addr) const;

    void setModel128(bool m) { is128 = m; }
    /** @brief Enable +2A/+3 paging (four ROM banks + uPD765). Implies 128K. */
    void setPlus3(bool on);
    /** @brief Opcode-fetch hook: page TR-DOS ROM in at 0x3D00–0x3DFF. */
    void m1_notify(uint16_t addr);
    /** @brief RAM bank currently mapped at 0xC000 (0 on 48K). */
    uint8_t paged_bank() const { return is128 ? static_cast<uint8_t>(port7ffd & 7) : 0; }
    /** @brief +3 ROM index 0..3 from 7FFD/1FFD, or 0/1 on 128K. */
    int rom_index() const
    {
        if (plus3)
        {
            return ((port1ffd >> 1) & 2) | ((port7ffd >> 4) & 1);
        }
        return (is128 && (port7ffd & 0x10)) ? 1 : 0;
    }

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

    uint64_t beeper_transition_tstates;
    uint64_t last_beeper_state;
    bool beeper_state;
    bool beeper_changed;
    void beeperSet(bool on);
    float currentAudioSample() const;
};

[[gnu::always_inline]] inline bool ULA::isContended(uint16_t addr) const
{
    const bool bank5 = (addr & 0xC000u) == 0x4000u;
    const bool odd_c000 = is128 && (addr >= 0xC000u) && ((port7ffd & 1u) != 0);
    if (!bank5 && !odd_c000)
    {
        return false;
    }
    return static_cast<unsigned>(line - ULA_FIRST_LINE) < 192u
        && static_cast<unsigned>(line_tstates - (ULA_FIRST_PIXEL - 1)) < 129u;
}

[[gnu::always_inline]] inline uint8_t ULA::read(uint16_t addr)
{
    if (plus3 && (port1ffd & 0x01))
    {
        static const int maps[4][4] = {
            {0, 1, 2, 3},
            {4, 5, 6, 7},
            {4, 5, 6, 3},
            {4, 7, 6, 3}
        };
        const int mode = (port1ffd >> 1) & 3;
        const int slot = addr >> 14;
        return ram_banks[maps[mode][slot]][addr & 0x3FFF];
    }
    if (addr < 0x4000)
    {
        if (trdos_paged && trdos_present)
        {
            return trdos_rom[addr];
        }
        switch (rom_index())
        {
            case 1:
                return rom1[addr];
            case 2:
                return rom2[addr];
            case 3:
                return rom3[addr];
            default:
                return rom[addr];
        }
    }
    if (addr < 0x8000)
    {
        return ram_banks[5][addr - 0x4000];
    }
    if (addr < 0xC000)
    {
        return ram_banks[2][addr - 0x8000];
    }
    return ram_banks[paged_bank()][addr - 0xC000];
}
