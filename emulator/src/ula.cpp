#include "ula.h"
#include <cstring>
#include <cstdio>

ULA::ULA() : border(0), beeper(false), tstates(0), frame_tstates(0), line(0), line_tstates(0), flash(false), flash_counter(0), kempston(0),
             beeper_transition_tstates(0), last_beeper_state(0), beeper_state(false), beeper_changed(false) {
    reset();
}

void ULA::resetToSyntheticROM() {
    memset(rom, 0, sizeof(rom));
    // Minimal synthetic Spectrum 48K ROM. This avoids the copyrighted
    // Sinclair ROM while providing the bare minimum a self-contained game needs.
    rom[0x0000] = 0xF3; // DI
    rom[0x0001] = 0xC3; // JP
    rom[0x0002] = 0x03; // low byte of snapshot PC (Manic Miner)
    rom[0x0003] = 0x93; // high byte of snapshot PC (Manic Miner)
    rom[0x0038] = 0xFB; // EI
    rom[0x0039] = 0xC9; // RET
}

void ULA::reset() {
    memset(ram, 0, sizeof(ram));
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
    resetToSyntheticROM();
}

uint8_t ULA::read(uint16_t addr) {
    if (addr < 0x4000) return rom[addr];
    return ram[addr - 0x4000];
}

void ULA::write(uint16_t addr, uint8_t val) {
    if (addr >= 0x4000) ram[addr - 0x4000] = val;
}

uint8_t ULA::ioRead(uint16_t port) {
    uint8_t p = (uint8_t)(port & 0xFF);
    if (p == 0x1F) return kempston;
    if ((p & 0x01) == 0) {
        uint8_t addr = (uint8_t)(port >> 8);
        uint8_t result = 0xFF;
        for (int i = 0; i < 8; i++) {
            if (!(addr & (1 << i))) {
                result &= keyboard[i];
            }
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
    return beeper_state ? 0.25f : -0.25f;
}

void ULA::ioWrite(uint16_t port, uint8_t val) {
    uint8_t p = (uint8_t)(port & 0xFF);
    if ((p & 0x01) == 0) {
        border = val & 0x07;
        beeperSet((val & 0x10) != 0);
        // Diagnostic console output: if bit 4 is clear, treat low 7 bits as
        // printable ASCII. Used by test stubs (e.g. ZEXALL CP/M BDOS fn 9).
        if ((val & 0x10) == 0) {
            char c = val & 0x7F;
            if (c == '\r') c = '\n';
            if (c >= 0x20 || c == '\n') {
                fputc(c, stderr);
                fflush(stderr);
            }
        }
    }
}

void ULA::step(int cycles) {
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
    if (addr < 0x4000 || addr > 0x7FFF) return false;
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

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int pixel_y = y;
        int char_y = pixel_y >> 3;
        int line_y = pixel_y & 7;

        uint32_t* line = pixels + y * (pitch / 4);

        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int pixel_x = x;
            int char_x = pixel_x >> 3;
            int line_x = 7 - (pixel_x & 7);

            int bitmap_addr = 0x4000 + ((char_y & 0x18) << 8) + ((char_y & 0x07) << 5) + (char_x);
            int attr_addr = 0x5800 + (char_y << 5) + char_x;

            uint8_t bitmap = ram[bitmap_addr - 0x4000];
            uint8_t attr = ram[attr_addr - 0x4000];

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

            line[x] = palette[color_idx];
        }
    }
}

void ULA::setKey(int row, int bit, bool pressed) {
    if (pressed) keyboard[row] &= ~(1 << bit);
    else keyboard[row] |= (1 << bit);
}
