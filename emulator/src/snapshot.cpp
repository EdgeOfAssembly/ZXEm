#include "snapshot.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

// Z80 snapshot v1 format (used by 48K Manic Miner):
//   - 30 byte header
//   - if PC at header bytes 6/7 is non-zero, it's v1
//   - byte 12 bit 5 = compression flag
//   - v1 compressed RAM follows immediately after the header and ends with
//     an ED ED 00 ED marker (which decodes as a single 0xED byte)

static bool decompressV1(FILE* f, ULA& ula, uint32_t& out_addr) {
    uint32_t addr = 0x4000;
    while (addr < 0x10000) {
        int c = fgetc(f);
        if (c == EOF) break;
        uint8_t b = (uint8_t)c;

        if (b == 0xED) {
            int d = fgetc(f);
            if (d == EOF) break;
            if (d == 0xED) {
                int rep = fgetc(f);
                int val = fgetc(f);
                if (rep == EOF || val == EOF) break;
                // Standard v1 convention: ED ED 00 ED is the terminator.
                // It represents a single literal 0xED byte.
                if (rep == 0x00 && val == 0xED) {
                    ula.write((uint16_t)addr++, 0xED);
                    break;
                }
                int count = rep ? rep : 256;
                for (int i = 0; i < count && addr < 0x10000; i++) {
                    ula.write((uint16_t)addr++, (uint8_t)val);
                }
            } else {
                ula.write((uint16_t)addr++, b);
                ula.write((uint16_t)addr++, (uint8_t)d);
            }
        } else {
            ula.write((uint16_t)addr++, b);
        }
    }
    out_addr = addr;
    return addr >= 0x10000;
}

bool loadZ80(const char* path, Z80& z80, ULA& ula) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    uint8_t header[30];
    if (fread(header, 1, 30, f) != 30) { fclose(f); return false; }

    z80.A = header[0];
    z80.F = header[1];
    z80.C = header[2];
    z80.B = header[3];
    z80.L = header[4];
    z80.H = header[5];

    z80.PC = header[6] | ((uint16_t)header[7] << 8);
    z80.SP = header[8] | ((uint16_t)header[9] << 8);

    z80.I = header[10];
    z80.R = header[11];

    uint8_t byte12 = header[12];
    bool compressed = (byte12 & 0x20) != 0;
    z80.R = (z80.R & 0x7F) | ((byte12 & 1) ? 0x80 : 0);

    z80.E = header[13];
    z80.D = header[14];
    z80.C_ = header[15];
    z80.B_ = header[16];
    z80.E_ = header[17];
    z80.D_ = header[18];
    z80.L_ = header[19];
    z80.H_ = header[20];
    z80.A_ = header[21];
    z80.F_ = header[22];

    z80.IY = header[23] | ((uint16_t)header[24] << 8);
    z80.IX = header[25] | ((uint16_t)header[26] << 8);

    z80.IFF1 = (header[27] & 1) != 0;
    z80.IFF2 = (header[27] & 4) != 0;
    z80.IM = header[29] & 3;

    fprintf(stderr, "Snapshot: PC=0x%04X SP=0x%04X A=0x%02X F=0x%02X "
                  "BC=0x%04X DE=0x%04X HL=0x%04X IX=0x%04X IY=0x%04X IM=%d %s\n",
            z80.PC, z80.SP, z80.A, z80.F,
            (z80.B << 8) | z80.C, (z80.D << 8) | z80.E, (z80.H << 8) | z80.L,
            z80.IX, z80.IY, z80.IM, compressed ? "compressed" : "raw");

    if (z80.PC == 0) {
        // v2/v3 extended snapshot - not supported by this minimal loader
        fclose(f);
        fprintf(stderr, "Error: extended v2/v3 Z80 snapshots are not supported.\n");
        return false;
    }

    if (compressed) {
        uint32_t final_addr = 0x4000;
        if (!decompressV1(f, ula, final_addr)) {
            fclose(f);
            fprintf(stderr, "Warning: v1 decompress did not reach 0x10000 (loaded %u bytes)\n", (unsigned)(final_addr - 0x4000));
            return false;
        }
    } else {
        uint8_t buf[49152];
        size_t n = fread(buf, 1, 49152, f);
        for (size_t i = 0; i < n; i++) {
            ula.write(0x4000 + (uint16_t)i, buf[i]);
        }
    }

    fclose(f);
    return true;
}

bool saveZ80(const char* path, const Z80& z80, const ULA& ula) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    uint8_t header[30];
    memset(header, 0, sizeof(header));
    header[0] = z80.A;
    header[1] = z80.F;
    header[2] = z80.C;
    header[3] = z80.B;
    header[4] = z80.L;
    header[5] = z80.H;
    header[6] = z80.PC & 0xFF;
    header[7] = z80.PC >> 8;
    header[8] = z80.SP & 0xFF;
    header[9] = z80.SP >> 8;
    header[10] = z80.I;
    header[11] = z80.R & 0x7F;
    header[12] = 0x20 | ((z80.R >> 7) & 1); // compressed flag + R7
    header[13] = z80.E;
    header[14] = z80.D;
    header[15] = z80.C_;
    header[16] = z80.B_;
    header[17] = z80.E_;
    header[18] = z80.D_;
    header[19] = z80.L_;
    header[20] = z80.H_;
    header[21] = z80.A_;
    header[22] = z80.F_;
    header[23] = z80.IY & 0xFF;
    header[24] = z80.IY >> 8;
    header[25] = z80.IX & 0xFF;
    header[26] = z80.IX >> 8;
    header[27] = (z80.IFF1 ? 1 : 0) | (z80.IFF2 ? 4 : 0);
    header[29] = z80.IM & 3;

    fwrite(header, 1, 30, f);

    // Simple v1 compressor: emit literal runs, use RLE for runs >= 4.
    uint16_t addr = 0x4000;
    while (true) {
        uint8_t v = ula.ram[addr - 0x4000];
        uint16_t run = 1;
        while ((uint32_t)(addr - 0x4000 + run) < 49152U && ula.ram[addr - 0x4000 + run] == v && run < 255) run++;

        if (run >= 4) {
            fputc(0xED, f);
            fputc(0xED, f);
            fputc(run & 0xFF, f);
            fputc(v, f);
            addr += run;
        } else {
            for (uint16_t i = 0; i < run; i++) {
                uint8_t b = ula.ram[addr + i - 0x4000];
                fputc(b, f);
                if (b == 0xED) fputc(0xED, f); // escape literal ED
            }
            addr += run;
        }
        if (addr == 0) break; // wrapped past 0xFFFF
    }

    // v1 terminator: literal 0xED encoded as ED ED 00 ED
    fputc(0xED, f);
    fputc(0xED, f);
    fputc(0x00, f);
    fputc(0xED, f);

    fclose(f);
    return true;
}

bool loadROM(const char* path, ULA& ula) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t tmp[16384];
    size_t n = fread(tmp, 1, 16384, f);
    fclose(f);
    if (n != 16384) {
        fprintf(stderr, "ROM file %s is not 16384 bytes (got %zu)\n", path, n);
        return false;
    }
    memcpy(ula.rom, tmp, 16384);
    fprintf(stderr, "Loaded ROM: %s\n", path);
    return true;
}

bool loadROMFromDir(const char* dir, ULA& ula) {
    DIR* d = opendir(dir);
    if (!d) return false;

    // Look for common Spectrum 48K ROM filenames in order of preference.
    static const char* candidates[] = {
        "48.rom", "spectrum.rom", " Sinclair Spectrum 48K.rom",
        "48K.ROM", "48k.rom", "rom.rom", "zx.rom"
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, candidates[i]);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            bool ok = loadROM(path, ula);
            closedir(d);
            return ok;
        }
    }

    // Otherwise load the first file that is exactly 16384 bytes.
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (st.st_size == 16384) {
            bool ok = loadROM(path, ula);
            closedir(d);
            return ok;
        }
    }

    closedir(d);
    return false;
}
