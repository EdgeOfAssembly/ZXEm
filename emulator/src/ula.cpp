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

int ULA::rom_index() const
{
    if (plus3)
    {
        return ((port1ffd >> 1) & 2) | ((port7ffd >> 4) & 1);
    }
    return (is128 && (port7ffd & 0x10)) ? 1 : 0;
}

uint8_t ULA::paged_bank() const
{
    return is128 ? static_cast<uint8_t>(port7ffd & 0x07) : 0;
}

uint8_t ULA::read(uint16_t addr) {
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
    if (addr < 0x4000) {
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
    /* 48K and 128K share the same CPU map: 5 / 2 / paged. */
    if (addr < 0x8000) {
        return ram_banks[5][addr - 0x4000];
    }
    if (addr < 0xC000) {
        return ram_banks[2][addr - 0x8000];
    }
    return ram_banks[paged_bank()][addr - 0xC000];
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
    tape.step(cycles);
    if (is128) ay.step(cycles);
    for (int i = 0; i < cycles; i++) {
        tstates++;
        frame_tstates++;
        line_tstates++;

        if (line_tstates >= TSTATES_PER_LINE) {
            line_tstates = 0;
            line++;
            if (line >= LINES_PER_FRAME) {
                line = 0;
                frame_tstates = 0;
                flash_counter++;
                if (flash_counter >= 16) {
                    flash_counter = 0;
                    flash = !flash;
                }
            }
        }
    }
}

bool ULA::isContended(uint16_t addr, int tstate) {
    bool contended_addr = false;
    if (addr >= 0x4000 && addr <= 0x7FFF)
    {
        contended_addr = true;
    }
    else if (is128 && addr >= 0xC000)
    {
        /* Odd-numbered banks are contended on 128K. */
        contended_addr = (paged_bank() & 1) != 0;
    }
    if (!contended_addr)
    {
        return false;
    }
    int line_ts = tstate % TSTATES_PER_LINE;
    int cur_line = (tstate / TSTATES_PER_LINE) % LINES_PER_FRAME;
    if (cur_line < ULA_FIRST_LINE || cur_line >= ULA_LAST_LINE) return false;
    if (line_ts < ULA_FIRST_PIXEL - 1 || line_ts >= ULA_LAST_PIXEL) return false;
    return true;
}

void ULA::renderFrame(uint32_t* pixels, int pitch) {
    static const uint32_t palette[16] = {
        0xFF000000, 0xFF0000CD, 0xFFCD0000, 0xFFCD00CD,
        0xFF00CD00, 0xFF00CDCD, 0xFFCDCD00, 0xFFCDCDCD,
        0xFF000000, 0xFF0000FF, 0xFFFF0000, 0xFFFF00FF,
        0xFF00FF00, 0xFF00FFFF, 0xFFFFFF00, 0xFFFFFFFF
    };

    int screen_bank = (is128 && (port7ffd & 0x08)) ? 7 : 5;
    uint8_t* scr_ram = ram_banks[screen_bank];

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int pixel_y = y;
        int char_y = pixel_y >> 3;
        uint32_t* row_pixels = pixels + y * (pitch / 4);

        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int pixel_x = x;
            int char_x = pixel_x >> 3;
            int line_x = 7 - (pixel_x & 7);

            int bitmap_addr = ((pixel_y & 0xC0) << 5) | ((pixel_y & 0x07) << 8) |
                              ((pixel_y & 0x38) << 2) | char_x;
            int attr_addr = 0x1800 + (char_y << 5) + char_x;

            uint8_t bitmap = scr_ram[bitmap_addr];
            uint8_t attr = scr_ram[attr_addr];

            int ink = attr & 0x07;
            int paper = (attr >> 3) & 0x07;
            int bright = (attr & 0x40) ? 8 : 0;
            bool flashing = (attr & 0x80) && flash;

            bool pixel_on = (bitmap >> line_x) & 1;
            int color_idx;
            if (pixel_on) {
                color_idx = flashing ? (paper + bright) : (ink + bright);
            } else {
                color_idx = flashing ? (ink + bright) : (paper + bright);
            }

            row_pixels[x] = palette[color_idx];
        }
    }
}

void ULA::setKey(int row, int bit, bool pressed) {
    if (pressed) keyboard[row] &= ~(1 << bit);
    else keyboard[row] |= (1 << bit);
}
