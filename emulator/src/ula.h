/**
 * @file ula.h
 * @brief ZX Spectrum ULA: memory, I/O, raster timing, and contention.
 */
#pragma once
#include <cstdint>
#include <cstring>
#include "ay.h"
#include "disk.h"
#include "tape.h"

class ULA {
public:
    /**
     * @brief Per-model raster and CPU clock (48K vs 128K/+3).
     *
     * 48K: 224 T/line × 312 lines = 69 888 T at 3.5 MHz (50.08 Hz).
     * 128K/+3: 228 T/line × 311 lines = 70 908 T at 3.5469 MHz (50.02 Hz).
     */
    struct Timing
    {
        int t_line;
        int lines;
        int t_frame;
        int cpu_hz;
    };
    static constexpr Timing kTiming48{224, 312, 69888, 3500000};
    static constexpr Timing kTiming128{228, 311, 70908, 3546900};
    Timing timing{kTiming48};
    uint8_t rom[16384];
    uint8_t rom1[16384];
    uint8_t rom2[16384];
    uint8_t rom3[16384];
    uint8_t trdos_rom[16384];
    uint8_t ram[49152];
    uint8_t ram_banks[8][16384];
    uint8_t border;
    /** @brief Last border colour (0–7) on each raster line (48K 312; 128K uses 311). */
    uint8_t border_line[312];
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
    /**
     * @brief Read an I/O port (keyboard, Kempston, AY, FDC, or floating bus).
     * @param[in] port Full 16-bit port address.
     * @return Port data. Unmapped odd ports yield @ref floating_bus.
     */
    uint8_t ioRead(uint16_t port);
    /**
     * @brief Write an I/O port. Even ports update @c border and @c border_line[line].
     * @param[in] port Full 16-bit port address.
     * @param[in] val  Byte written by the Z80.
     */
    void ioWrite(uint16_t port, uint8_t val);
    void step(int cycles);
    /**
     * @brief Consume the pending maskable frame INT (set when the raster wraps last line→0).
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
     * @brief Decode the 352×312 raster (border + 256×192 paper) into ARGB8888.
     * @param[out] pixels Destination; at least SCREEN_WIDTH×SCREEN_HEIGHT entries
     *                   if pitch is SCREEN_WIDTH×4.
     * @param[in] pitch Bytes per destination row (typically SCREEN_WIDTH * 4).
     */
    void renderFrame(uint32_t* pixels, int pitch);
    /**
     * @brief Extra T-states if this memory access waits on the ULA.
     * Bank 5 at $4000 and odd 128K banks at $C000, during the pixel window.
     * Uses @c line / @c line_tstates (no divide).
     * @param[in] addr 16-bit memory address.
     * @return Wait 0..6 (pattern 6,5,4,3,2,1,0,0 per 8 T); 0 if uncontended.
     */
    int isContended(uint16_t addr) const;

    /**
     * @brief Select 48K or 128K timing (t_line/lines/t_frame/cpu_hz).
     * @param[in] m true → 128K (228×311, 3.5469 MHz); false → 48K and not +3.
     */
    void setModel128(bool m)
    {
        is128 = m;
        if (m)
        {
            timing = kTiming128;
        }
        else
        {
            plus3 = false;
            timing = kTiming48;
        }
    }
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

    /** @brief Full PAL raster width: 48 left + 256 paper + 48 right. */
    static const int SCREEN_WIDTH = 352;
    /** @brief Full PAL raster height (48K 312 lines; 128K line 311 unused). */
    static const int SCREEN_HEIGHT = 312;
    /** @brief Paper (display file) width in pixels. */
    static const int PAPER_WIDTH = 256;
    /** @brief Paper (display file) height in pixels. */
    static const int PAPER_HEIGHT = 192;
    /** @brief Left-border width; paper is blitted at this x. */
    static const int BORDER_LEFT = 48;
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

    bool beeper_state;
    /** @brief I/O contention charged in ioRead/ioWrite; consumed by Z80. */
    int extra_wait = 0;
    /**
     * @brief Set MIC/EAR beeper level from ULA port bit 4.
     * @param[in] on true → high (EAR bit set).
     */
    void beeperSet(bool on);
    /**
     * @brief Mix ULA beeper with AY-3-8912 output.
     * @return Combined sample; DC blocking is applied by the host, not here.
     */
    float currentAudioSample() const;

    int t_line() const { return timing.t_line; }
    int lines() const { return timing.lines; }
    int t_frame() const { return timing.t_frame; }
    int cpu_hz() const { return timing.cpu_hz; }
    /**
     * @brief Extra T-states for the current raster slot (6,5,4,3,2,1,0,0 or 0).
     * @return Wait in T-states; 0 outside the 192×128 pixel window.
     */
    int contention_delay() const;
    /**
     * @brief Extra T-states for an even ULA port (A0=0) in the pixel window.
     * @param[in] port Full 16-bit I/O address.
     * @return Same pattern as memory contention, or 0 if A0=1 or outside window.
     */
    int io_contention(uint16_t port) const;
    /**
     * @brief Idle-bus byte for an unmapped odd-port read (classic 48K/128K floating bus).
     * @return Display-file bitmap or attribute during the pixel window; 0xFF on idle, border, or +3.
     * @note +2A/+3 gate array does not expose the display fetch. Screen bank follows 7FFD bit 3.
     */
    uint8_t floating_bus() const;
    /**
     * @brief Consume I/O wait accumulated by ioRead/ioWrite.
     * @return Extra T-states; the accumulator is cleared.
     */
    int take_extra_wait()
    {
        const int w = extra_wait;
        extra_wait = 0;
        return w;
    }
};

[[gnu::always_inline]] inline int ULA::contention_delay() const
{
    if (static_cast<unsigned>(line - ULA_FIRST_LINE) >= 192u)
    {
        return 0;
    }
    const int t = line_tstates - ULA_FIRST_PIXEL;
    if (static_cast<unsigned>(t) >= 128u)
    {
        return 0;
    }
    static constexpr int kPat[8] = {6, 5, 4, 3, 2, 1, 0, 0};
    return kPat[t & 7];
}

[[gnu::always_inline]] inline int ULA::io_contention(uint16_t port) const
{
    if ((port & 1u) != 0)
    {
        return 0;
    }
    return contention_delay();
}

[[gnu::always_inline]] inline uint8_t ULA::floating_bus() const
{
    /* +2A/+3 ASIC does not put display fetches on the idle bus. */
    if (plus3)
    {
        return 0xFF;
    }
    if (static_cast<unsigned>(line - ULA_FIRST_LINE) >= 192u)
    {
        return 0xFF;
    }
    const int t = line_tstates - ULA_FIRST_PIXEL;
    if (static_cast<unsigned>(t) >= 128u)
    {
        return 0xFF;
    }
    /* 8 T: bitmap, attr, bitmap+1, attr+1, then 4 T idle (0xFF). */
    const int phase = t & 7;
    if (phase >= 4)
    {
        return 0xFF;
    }
    const int y = line - ULA_FIRST_LINE;
    const int col = ((t >> 3) << 1) | ((phase >= 2) ? 1 : 0);
    const int screen_bank = (is128 && (port7ffd & 0x08)) ? 7 : 5;
    const uint8_t* const scr = ram_banks[screen_bank];
    if ((phase & 1) != 0)
    {
        return scr[0x1800 + ((y >> 3) << 5) + col];
    }
    const int bmp = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | col;
    return scr[bmp];
}

[[gnu::always_inline]] inline int ULA::isContended(uint16_t addr) const
{
    const bool bank5 = (addr & 0xC000u) == 0x4000u;
    const bool odd_c000 = is128 && (addr >= 0xC000u) && ((port7ffd & 1u) != 0);
    if (!bank5 && !odd_c000)
    {
        return 0;
    }
    return contention_delay();
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
