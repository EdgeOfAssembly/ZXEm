#include "ula.h"
#include "log.h"
#include <cstring>

ULA::ULA() : border(0), beeper(false), tstates(0), frame_tstates(0), line(0), line_tstates(0),
             flash(false), flash_counter(0),
             is128(false), plus3(false), trdos_present(false), trdos_paged(false),
             port7ffd(0), port1ffd(0),
             kempston(0),
             beeper_transition_tstates(0), last_beeper_state(0), beeper_state(false), beeper_changed(false) {
    reset();
}

void ULA::resetToSyntheticROM() {
    memset(rom, 0, sizeof(rom));
    memset(rom1, 0, sizeof(rom1));
    memset(rom2, 0, sizeof(rom2));
    memset(rom3, 0, sizeof(rom3));
    rom[0x0000] = 0xF3;
    rom[0x0001] = 0xC3;
    rom[0x0002] = 0x03;
    rom[0x0003] = 0x93;
    rom[0x0038] = 0xFB;
    rom[0x0039] = 0xC9;
    memcpy(rom1, rom, 16384);
    memcpy(rom2, rom, 16384);
    memcpy(rom3, rom, 16384);
}

void ULA::reset() {
    memset(ram, 0, sizeof(ram));
    memset(ram_banks, 0, sizeof(ram_banks));
    memset(keyboard, 0xFF, sizeof(keyboard));
    border = 0;
    beeper = false;
    tstates = 0;
    frame_tstates = 0;
    line = 0;
    line_tstates = 0;
    flash = false;
    flash_counter = 0;
    kempston = 0;
    beeper_transition_tstates = 0;
    last_beeper_state = 0;
    beeper_state = false;
    beeper_changed = false;
    port7ffd = 0;
    port1ffd = 0;
    plus3 = false;
    trdos_paged = false;
    ay.reset();
    fdc.reset();
    fdc.disk = &edsk;
    beta.reset();
    tape.reset();
    resetToSyntheticROM();
}

void ULA::setPlus3(bool on)
{
    plus3 = on;
    if (on)
    {
        is128 = true;
    }
}

void ULA::m1_notify(uint16_t addr)
{
    if (!trdos_present)
    {
        return;
    }
    if (addr >= 0x3D00 && addr <= 0x3DFF)
    {
        trdos_paged = true;
    }
    else if (addr >= 0x4000)
    {
        trdos_paged = false;
    }
}



void ULA::write(uint16_t addr, uint8_t val) {
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
        ram_banks[maps[mode][slot]][addr & 0x3FFF] = val;
        if (addr >= 0x4000)
        {
            ram[addr - 0x4000] = val;
        }
        return;
    }
    if (addr < 0x4000) return;
    if (addr < 0x8000) {
        ram_banks[5][addr - 0x4000] = val;
        ram[addr - 0x4000] = val;
        return;
    }
    if (addr < 0xC000) {
        ram_banks[2][addr - 0x8000] = val;
        ram[addr - 0x4000] = val;
        return;
    }
    ram_banks[paged_bank()][addr - 0xC000] = val;
    ram[addr - 0x4000] = val;
}

uint8_t ULA::ioRead(uint16_t port) {
    uint8_t p = (uint8_t)(port & 0xFF);
    if (plus3)
    {
        if ((port & 0xF002) == 0x2000)
        {
            return fdc.read_msr();
        }
        if ((port & 0xF002) == 0x3000)
        {
            return fdc.read_data();
        }
    }
    if (trdos_paged && trdos_present)
    {
        if (p == 0x1F)
        {
            return beta.read_status();
        }
        if (p == 0x3F)
        {
            return beta.read_track();
        }
        if (p == 0x5F)
        {
            return beta.read_sector();
        }
        if (p == 0x7F)
        {
            return beta.read_data();
        }
        if (p == 0xFF)
        {
            return beta.read_system();
        }
    }
    if (p == 0x1F) return kempston;
    /* AY register read: A15=1 A14=1 A1=0 (0xFFFD). */
    if (is128 && (port & 0xC002) == 0xC000) {
        if (Log::instance().trace_io())
        {
            log_trace("io rd FFFD AY R%u = 0x%02X", ay.selected(), ay.read_data());
        }
        return ay.read_data();
    }
    if ((p & 0x01) == 0) {
        uint8_t addr = (uint8_t)(port >> 8);
        uint8_t result = 0xFF;
        for (int i = 0; i < 8; i++) {
            if (!(addr & (1 << i))) {
                result &= keyboard[i];
            }
        }
        if (!tape.ear_high())
        {
            result = static_cast<uint8_t>(result & ~0x40);
        }
        return result;
    }
    return 0xFF;
}

void ULA::beeperSet(bool on) {
    if (beeper_state != on) {
        beeper_state = on;
        beeper_transition_tstates = (uint64_t)tstates;
        beeper_changed = true;
    }
    beeper = on;
}

float ULA::currentAudioSample() const {
    float beep = beeper_state ? 0.25f : -0.25f;
    float ay_s = ay.sample() * 0.35f;
    return beep + ay_s;
}

void ULA::ioWrite(uint16_t port, uint8_t val) {
    uint8_t p = (uint8_t)(port & 0xFF);
    if ((p & 0x01) == 0) {
        border = val & 0x07;
        beeperSet((val & 0x10) != 0);
    }
    if (is128) {
        /* 128K paging: A15=0 A1=0 (0x7FFD). Bit 5 locks further writes. */
        if ((port & 0x8002) == 0)
        {
            if ((port7ffd & 0x20) == 0)
            {
                port7ffd = val;
                if (Log::instance().trace_io())
                {
                    log_trace("io wr 7FFD = 0x%02X bank=%u rom=%u shadow=%u lock=%u",
                              val, val & 7, (val >> 4) & 1, (val >> 3) & 1, (val >> 5) & 1);
                }
            }
        }
        /* AY data: A15=1 A14=0 A1=0 (0xBFFD). */
        if ((port & 0xC002) == 0x8000)
        {
            ay.write_data(val);
            if (Log::instance().trace_io())
            {
                log_trace("io wr BFFD AY R%u = 0x%02X", ay.selected(), val);
            }
        }
        /* AY latch: A15=1 A14=1 A1=0 (0xFFFD). */
        else if ((port & 0xC002) == 0xC000)
        {
            ay.select(val);
            if (Log::instance().trace_io())
            {
                log_trace("io wr FFFD AY select R%u", val & 0x0F);
            }
        }
        /* +2A/+3 1FFD: A15-A12 = 0001, A1=0. */
        if ((port & 0xF002) == 0x1000)
        {
            port1ffd = val;
        }
        if (plus3 && (port & 0xF002) == 0x3000)
        {
            fdc.write_data(val);
        }
    }
    if (trdos_paged && trdos_present)
    {
        if (p == 0x1F)
        {
            beta.write_command(val);
        }
        else if (p == 0x3F)
        {
            beta.write_track(val);
        }
        else if (p == 0x5F)
        {
            beta.write_sector(val);
        }
        else if (p == 0x7F)
        {
            beta.write_data(val);
        }
        else if (p == 0xFF)
        {
            beta.write_system(val);
        }
    }
    if (Log::instance().trace_io() && (p & 0x01) == 0)
    {
        log_trace("io wr ULA port=0x%04X val=0x%02X border=%u ear=%u",
                  port, val, val & 7, (val >> 4) & 1);
    }
}

void ULA::step(int cycles) {
    if (cycles <= 0)
    {
        return;
    }
    if (tape.loaded())
    {
        tape.step(cycles);
    }
    if (is128)
    {
        ay.step(cycles);
    }
    /* Advance T-states in line-sized chunks instead of a 1-T loop. */
    int left = cycles;
    while (left > 0)
    {
        const int room = TSTATES_PER_LINE - line_tstates;
        const int chunk = (left < room) ? left : room;
        tstates += chunk;
        frame_tstates += chunk;
        line_tstates += chunk;
        left -= chunk;
        if (line_tstates >= TSTATES_PER_LINE)
        {
            line_tstates = 0;
            line++;
            if (line >= LINES_PER_FRAME)
            {
                line = 0;
                frame_tstates = 0;
                flash_counter++;
                if (flash_counter >= 16)
                {
                    flash_counter = 0;
                    flash = !flash;
                }
            }
        }
    }
}



void ULA::renderFrame(uint32_t* pixels, int pitch) {
    static const uint32_t palette[16] = {
        0xFF000000, 0xFF0000CD, 0xFFCD0000, 0xFFCD00CD,
        0xFF00CD00, 0xFF00CDCD, 0xFFCDCD00, 0xFFCDCDCD,
        0xFF000000, 0xFF0000FF, 0xFFFF0000, 0xFFFF00FF,
        0xFF00FF00, 0xFF00FFFF, 0xFFFFFF00, 0xFFFFFFFF
    };
    static uint16_t line_bmp[SCREEN_HEIGHT];
    static bool line_bmp_ready = false;
    if (!line_bmp_ready)
    {
        for (int y = 0; y < SCREEN_HEIGHT; y++)
        {
            line_bmp[y] = static_cast<uint16_t>(
                ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2));
        }
        line_bmp_ready = true;
    }

    const int screen_bank = (is128 && (port7ffd & 0x08)) ? 7 : 5;
    const uint8_t* const scr = ram_banks[screen_bank];
    const int stride = pitch / 4;
    const bool flash_on = flash;

    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        const uint8_t* bits = scr + line_bmp[y];
        const uint8_t* attrs = scr + 0x1800 + ((y >> 3) << 5);
        uint32_t* dst = pixels + y * stride;
        for (int col = 0; col < 32; col++)
        {
            const uint8_t bitmap = bits[col];
            const uint8_t attr = attrs[col];
            const int bright = (attr & 0x40) ? 8 : 0;
            int ink = (attr & 0x07) + bright;
            int paper = ((attr >> 3) & 0x07) + bright;
            if ((attr & 0x80) && flash_on)
            {
                const int tmp = ink;
                ink = paper;
                paper = tmp;
            }
            const uint32_t c1 = palette[ink];
            const uint32_t c0 = palette[paper];
            dst[0] = (bitmap & 0x80) ? c1 : c0;
            dst[1] = (bitmap & 0x40) ? c1 : c0;
            dst[2] = (bitmap & 0x20) ? c1 : c0;
            dst[3] = (bitmap & 0x10) ? c1 : c0;
            dst[4] = (bitmap & 0x08) ? c1 : c0;
            dst[5] = (bitmap & 0x04) ? c1 : c0;
            dst[6] = (bitmap & 0x02) ? c1 : c0;
            dst[7] = (bitmap & 0x01) ? c1 : c0;
            dst += 8;
        }
    }
}

void ULA::setKey(int row, int bit, bool pressed) {
    if (pressed) keyboard[row] &= ~(1 << bit);
    else keyboard[row] |= (1 << bit);
}
