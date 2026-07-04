#include "z80.h"
#include "ula.h"
#include <cstdio>
#include <cstdlib>

Z80::Z80() : ula(nullptr), tstates(0) { reset(); }

void Z80::reset() {
    A = F = B = C = D = E = H = L = 0;
    A_ = F_ = B_ = C_ = D_ = E_ = H_ = L_ = 0;
    IX = IY = SP = PC = 0;
    I = R = 0;
    IFF1 = IFF2 = false;
    IM = 0;
    halted = false;
    ei_pending = false;
    nmi_pending = false;
    tstates = 0;
}

bool Z80::parity(uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return !(v & 1);
}

uint8_t Z80::memRead(uint16_t addr) {
    int t = ula->tstates;
    uint8_t v = ula->read(addr);
    if (ula->isContended(addr, t)) tstates++;
    return v;
}

void Z80::memWrite(uint16_t addr, uint8_t val) {
    int t = ula->tstates;
    ula->write(addr, val);
    if (ula->isContended(addr, t)) tstates++;
}

uint8_t Z80::ioRead(uint16_t port) {
    return ula->ioRead(port);
}

void Z80::ioWrite(uint16_t port, uint8_t val) {
    ula->ioWrite(port, val);
}

uint8_t Z80::fetch8() {
    uint8_t v = memRead(PC);
    PC++;
    return v;
}

uint16_t Z80::fetch16() {
    uint8_t lo = fetch8();
    uint8_t hi = fetch8();
    return ((uint16_t)hi << 8) | lo;
}

uint8_t Z80::add8(uint8_t a, uint8_t b, bool carry) {
    int c = carry ? ((F & 1) ? 1 : 0) : 0;
    int result = a + b + c;
    int lookup = (a & 0x88) >> 3 | (b & 0x88) >> 2 | ((result & 0x88) >> 1);
    static const uint8_t half_carry_table[] = {0,0,1,0,1,0,1,1};
    F = 0;
    setFlagH(half_carry_table[lookup & 7]);
    setFlagC(result > 0xFF);
    uint8_t r = (uint8_t)result;
    setFlagS(r);
    setFlagZ(r);
    setFlagPV(((a ^ r) & (b ^ r) & 0x80) != 0);
    setFlag53(r);
    return r;
}

uint8_t Z80::sub8(uint8_t a, uint8_t b, bool carry) {
    int c = carry ? ((F & 1) ? 1 : 0) : 0;
    int result = a - b - c;
    int lookup = (a & 0x88) >> 3 | (b & 0x88) >> 2 | ((result & 0x88) >> 1);
    static const uint8_t half_carry_table[] = {0,1,1,0,1,0,0,1};
    F = 0x02;
    setFlagH(half_carry_table[lookup & 7]);
    setFlagC(result < 0);
    uint8_t r = (uint8_t)result;
    setFlagS(r);
    setFlagZ(r);
    setFlagPV(((a ^ b) & (a ^ r) & 0x80) != 0);
    setFlag53(r);
    return r;
}

uint8_t Z80::inc8(uint8_t a) {
    uint8_t r = a + 1;
    F = F & 0x01;
    setFlagH((a & 0x0F) == 0x0F);
    setFlagPV(a == 0x7F);
    setFlagS(r);
    setFlagZ(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::dec8(uint8_t a) {
    uint8_t r = a - 1;
    F = (F & 0x01) | 0x02;
    setFlagH((a & 0x0F) == 0x00);
    setFlagPV(a == 0x80);
    setFlagS(r);
    setFlagZ(r);
    setFlag53(r);
    return r;
}

void Z80::and8(uint8_t v) {
    A &= v;
    F = 0x10;
    setFlagSZP(A);
    setFlag53(A);
}

void Z80::xor8(uint8_t v) {
    A ^= v;
    F = 0;
    setFlagSZP(A);
    setFlag53(A);
}

void Z80::or8(uint8_t v) {
    A |= v;
    F = 0;
    setFlagSZP(A);
    setFlag53(A);
}

void Z80::cp8(uint8_t v) {
    sub8(A, v, false);
}

uint16_t Z80::add16(uint16_t a, uint16_t b) {
    int result = a + b;
    F = (F & 0xC4);
    setFlagH(((a & 0x0FFF) + (b & 0x0FFF)) > 0x0FFF);
    setFlagC(result > 0xFFFF);
    setFlag53((uint8_t)(result >> 8));
    return (uint16_t)result;
}

uint8_t Z80::rlc(uint8_t v) {
    bool c = (v & 0x80) != 0;
    uint8_t r = (v << 1) | (c ? 1 : 0);
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::rrc(uint8_t v) {
    bool c = (v & 0x01) != 0;
    uint8_t r = (v >> 1) | (c ? 0x80 : 0);
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::rl(uint8_t v) {
    bool c = (v & 0x80) != 0;
    uint8_t r = (v << 1) | ((F & 1) ? 1 : 0);
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::rr(uint8_t v) {
    bool c = (v & 0x01) != 0;
    uint8_t r = (v >> 1) | ((F & 1) ? 0x80 : 0);
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::sla(uint8_t v) {
    bool c = (v & 0x80) != 0;
    uint8_t r = v << 1;
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::sra(uint8_t v) {
    bool c = (v & 0x01) != 0;
    uint8_t r = (v >> 1) | (v & 0x80);
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::sll(uint8_t v) {
    bool c = (v & 0x80) != 0;
    uint8_t r = (v << 1) | 1;
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

uint8_t Z80::srl(uint8_t v) {
    bool c = (v & 0x01) != 0;
    uint8_t r = v >> 1;
    F = 0;
    setFlagC(c);
    setFlagSZP(r);
    setFlag53(r);
    return r;
}

void Z80::bit(uint8_t n, uint8_t v) {
    F = (F & 0x01) | 0x10;
    setFlagZ(v & (1 << n));
    setFlagPV(!(v & (1 << n)));
    setFlagS((n == 7) && (v & 0x80));
    setFlag53(v);
}

int Z80::execute() {
    if (nmi_pending) {
        nmi_pending = false;
        halted = false;
        IFF2 = IFF1;
        IFF1 = false;
        tstates += 11;
        memWrite(--SP, (uint8_t)(PC >> 8));
        memWrite(--SP, (uint8_t)(PC & 0xFF));
        PC = 0x0066;
        return 11;
    }

    if (ei_pending) {
        IFF1 = IFF2 = true;
        ei_pending = false;
    }

    if (halted) {
        tstates += 4;
        ula->step(4);
        return 4;
    }

    R = (R & 0x80) | ((R + 1) & 0x7F);

    uint8_t op = fetch8();
    int base_t = 4;

    switch (op) {
        case 0x00: break; // NOP
        case 0x01: setBC(fetch16()); base_t = 10; break;
        case 0x02: memWrite(getBC(), A); base_t = 7; break;
        case 0x03: setBC(getBC() + 1); base_t = 6; break;
        case 0x04: B = inc8(B); break;
        case 0x05: B = dec8(B); break;
        case 0x06: B = fetch8(); base_t = 7; break;
        case 0x07: { bool c = (A & 0x80) != 0; A = (A << 1) | (c ? 1 : 0); F = (F & 0xEC) | (c ? 1 : 0); setFlag53(A); } break;
        case 0x08: { uint16_t v = fetch16(); memWrite(v, (uint8_t)(getAF() & 0xFF)); memWrite(v + 1, (uint8_t)(getAF() >> 8)); base_t = 20; } break;
        case 0x09: setHL(add16(getHL(), getBC())); base_t = 11; break;
        case 0x0A: A = memRead(getBC()); base_t = 7; break;
        case 0x0B: setBC(getBC() - 1); base_t = 6; break;
        case 0x0C: C = inc8(C); break;
        case 0x0D: C = dec8(C); break;
        case 0x0E: C = fetch8(); base_t = 7; break;
        case 0x0F: { bool c = (A & 0x01) != 0; A = (A >> 1) | (c ? 0x80 : 0); F = (F & 0xEC) | (c ? 1 : 0); setFlag53(A); } break;

        case 0x10: { int8_t d = (int8_t)fetch8(); B = dec8(B); if (B != 0) { PC += d; base_t = 13; } else base_t = 8; } break;
        case 0x11: setDE(fetch16()); base_t = 10; break;
        case 0x12: memWrite(getDE(), A); base_t = 7; break;
        case 0x13: setDE(getDE() + 1); base_t = 6; break;
        case 0x14: D = inc8(D); break;
        case 0x15: D = dec8(D); break;
        case 0x16: D = fetch8(); base_t = 7; break;
        case 0x17: { bool c = (A & 0x80) != 0; A = (A << 1) | ((F & 1) ? 1 : 0); F = (F & 0xEC) | (c ? 1 : 0); setFlag53(A); } break;
        case 0x18: { int8_t d = (int8_t)fetch8(); PC += d; base_t = 12; } break;
        case 0x19: setHL(add16(getHL(), getDE())); base_t = 11; break;
        case 0x1A: A = memRead(getDE()); base_t = 7; break;
        case 0x1B: setDE(getDE() - 1); base_t = 6; break;
        case 0x1C: E = inc8(E); break;
        case 0x1D: E = dec8(E); break;
        case 0x1E: E = fetch8(); base_t = 7; break;
        case 0x1F: { bool c = (A & 0x01) != 0; A = (A >> 1) | ((F & 1) ? 0x80 : 0); F = (F & 0xEC) | (c ? 1 : 0); setFlag53(A); } break;

        case 0x20: { int8_t d = (int8_t)fetch8(); if (!(F & 0x40)) { PC += d; base_t = 12; } else base_t = 7; } break;
        case 0x21: setHL(fetch16()); base_t = 10; break;
        case 0x22: { uint16_t a = fetch16(); memWrite(a, L); memWrite(a + 1, H); base_t = 16; } break;
        case 0x23: setHL(getHL() + 1); base_t = 6; break;
        case 0x24: H = inc8(H); break;
        case 0x25: H = dec8(H); break;
        case 0x26: H = fetch8(); base_t = 7; break;
        case 0x27: { // DAA
            uint8_t c = 0;
            if ((F & 0x10) || ((F & 0x02) && (A & 0x0F) > 9)) c |= 0x06;
            if ((F & 0x01) || (!(F & 0x02) && A > 0x99)) c |= 0x60;
            if (F & 0x02) A -= c; else A += c;
            F = (F & 0x02);
            setFlagC(c >= 0x60);
            setFlagH(false);
            setFlagSZP(A);
            setFlag53(A);
        } break;
        case 0x28: { int8_t d = (int8_t)fetch8(); if (F & 0x40) { PC += d; base_t = 12; } else base_t = 7; } break;
        case 0x29: setHL(add16(getHL(), getHL())); base_t = 11; break;
        case 0x2A: { uint16_t a = fetch16(); L = memRead(a); H = memRead(a + 1); base_t = 16; } break;
        case 0x2B: setHL(getHL() - 1); base_t = 6; break;
        case 0x2C: L = inc8(L); break;
        case 0x2D: L = dec8(L); break;
        case 0x2E: L = fetch8(); base_t = 7; break;
        case 0x2F: A = ~A; F = (F & 0xC5) | 0x12; setFlag53(A); break;

        case 0x30: { int8_t d = (int8_t)fetch8(); if (!(F & 0x01)) { PC += d; base_t = 12; } else base_t = 7; } break;
        case 0x31: SP = fetch16(); base_t = 10; break;
        case 0x32: { uint16_t a = fetch16(); memWrite(a, A); base_t = 13; } break;
        case 0x33: SP++; base_t = 6; break;
        case 0x34: { uint16_t a = getHL(); uint8_t v = inc8(memRead(a)); memWrite(a, v); base_t = 11; } break;
        case 0x35: { uint16_t a = getHL(); uint8_t v = dec8(memRead(a)); memWrite(a, v); base_t = 11; } break;
        case 0x36: memWrite(getHL(), fetch8()); base_t = 10; break;
        case 0x37: F = (F & 0xC4) | 0x01; setFlag53(A); break;
        case 0x38: { int8_t d = (int8_t)fetch8(); if (F & 0x01) { PC += d; base_t = 12; } else base_t = 7; } break;
        case 0x39: setHL(add16(getHL(), SP)); base_t = 11; break;
        case 0x3A: A = memRead(fetch16()); base_t = 13; break;
        case 0x3B: SP--; base_t = 6; break;
        case 0x3C: A = inc8(A); break;
        case 0x3D: A = dec8(A); break;
        case 0x3E: A = fetch8(); base_t = 7; break;
        case 0x3F: F = (F & 0xC4) | ((F & 1) ? 0x10 : 0) | ((F & 1) ? 0 : 1); setFlag53(A); break;

        case 0x40: B = B; break;
        case 0x41: B = C; break;
        case 0x42: B = D; break;
        case 0x43: B = E; break;
        case 0x44: B = H; break;
        case 0x45: B = L; break;
        case 0x46: B = memRead(getHL()); base_t = 7; break;
        case 0x47: B = A; break;
        case 0x48: C = B; break;
        case 0x49: C = C; break;
        case 0x4A: C = D; break;
        case 0x4B: C = E; break;
        case 0x4C: C = H; break;
        case 0x4D: C = L; break;
        case 0x4E: C = memRead(getHL()); base_t = 7; break;
        case 0x4F: C = A; break;

        case 0x50: D = B; break;
        case 0x51: D = C; break;
        case 0x52: D = D; break;
        case 0x53: D = E; break;
        case 0x54: D = H; break;
        case 0x55: D = L; break;
        case 0x56: D = memRead(getHL()); base_t = 7; break;
        case 0x57: D = A; break;
        case 0x58: E = B; break;
        case 0x59: E = C; break;
        case 0x5A: E = D; break;
        case 0x5B: E = E; break;
        case 0x5C: E = H; break;
        case 0x5D: E = L; break;
        case 0x5E: E = memRead(getHL()); base_t = 7; break;
        case 0x5F: E = A; break;

        case 0x60: H = B; break;
        case 0x61: H = C; break;
        case 0x62: H = D; break;
        case 0x63: H = E; break;
        case 0x64: H = H; break;
        case 0x65: H = L; break;
        case 0x66: H = memRead(getHL()); base_t = 7; break;
        case 0x67: H = A; break;
        case 0x68: L = B; break;
        case 0x69: L = C; break;
        case 0x6A: L = D; break;
        case 0x6B: L = E; break;
        case 0x6C: L = H; break;
        case 0x6D: L = L; break;
        case 0x6E: L = memRead(getHL()); base_t = 7; break;
        case 0x6F: L = A; break;

        case 0x70: memWrite(getHL(), B); base_t = 7; break;
        case 0x71: memWrite(getHL(), C); base_t = 7; break;
        case 0x72: memWrite(getHL(), D); base_t = 7; break;
        case 0x73: memWrite(getHL(), E); base_t = 7; break;
        case 0x74: memWrite(getHL(), H); base_t = 7; break;
        case 0x75: memWrite(getHL(), L); base_t = 7; break;
        case 0x76: halted = true; break;
        case 0x77: memWrite(getHL(), A); base_t = 7; break;
        case 0x78: A = B; break;
        case 0x79: A = C; break;
        case 0x7A: A = D; break;
        case 0x7B: A = E; break;
        case 0x7C: A = H; break;
        case 0x7D: A = L; break;
        case 0x7E: A = memRead(getHL()); base_t = 7; break;
        case 0x7F: A = A; break;

        case 0x80: A = add8(A, B, false); break;
        case 0x81: A = add8(A, C, false); break;
        case 0x82: A = add8(A, D, false); break;
        case 0x83: A = add8(A, E, false); break;
        case 0x84: A = add8(A, H, false); break;
        case 0x85: A = add8(A, L, false); break;
        case 0x86: A = add8(A, memRead(getHL()), false); base_t = 7; break;
        case 0x87: A = add8(A, A, false); break;
        case 0x88: A = add8(A, B, true); break;
        case 0x89: A = add8(A, C, true); break;
        case 0x8A: A = add8(A, D, true); break;
        case 0x8B: A = add8(A, E, true); break;
        case 0x8C: A = add8(A, H, true); break;
        case 0x8D: A = add8(A, L, true); break;
        case 0x8E: A = add8(A, memRead(getHL()), true); base_t = 7; break;
        case 0x8F: A = add8(A, A, true); break;

        case 0x90: A = sub8(A, B, false); break;
        case 0x91: A = sub8(A, C, false); break;
        case 0x92: A = sub8(A, D, false); break;
        case 0x93: A = sub8(A, E, false); break;
        case 0x94: A = sub8(A, H, false); break;
        case 0x95: A = sub8(A, L, false); break;
        case 0x96: A = sub8(A, memRead(getHL()), false); base_t = 7; break;
        case 0x97: A = sub8(A, A, false); break;
        case 0x98: A = sub8(A, B, true); break;
        case 0x99: A = sub8(A, C, true); break;
        case 0x9A: A = sub8(A, D, true); break;
        case 0x9B: A = sub8(A, E, true); break;
        case 0x9C: A = sub8(A, H, true); break;
        case 0x9D: A = sub8(A, L, true); break;
        case 0x9E: A = sub8(A, memRead(getHL()), true); base_t = 7; break;
        case 0x9F: A = sub8(A, A, true); break;

        case 0xA0: and8(B); break;
        case 0xA1: and8(C); break;
        case 0xA2: and8(D); break;
        case 0xA3: and8(E); break;
        case 0xA4: and8(H); break;
        case 0xA5: and8(L); break;
        case 0xA6: and8(memRead(getHL())); base_t = 7; break;
        case 0xA7: and8(A); break;
        case 0xA8: xor8(B); break;
        case 0xA9: xor8(C); break;
        case 0xAA: xor8(D); break;
        case 0xAB: xor8(E); break;
        case 0xAC: xor8(H); break;
        case 0xAD: xor8(L); break;
        case 0xAE: xor8(memRead(getHL())); base_t = 7; break;
        case 0xAF: xor8(A); break;

        case 0xB0: or8(B); break;
        case 0xB1: or8(C); break;
        case 0xB2: or8(D); break;
        case 0xB3: or8(E); break;
        case 0xB4: or8(H); break;
        case 0xB5: or8(L); break;
        case 0xB6: or8(memRead(getHL())); base_t = 7; break;
        case 0xB7: or8(A); break;
        case 0xB8: cp8(B); break;
        case 0xB9: cp8(C); break;
        case 0xBA: cp8(D); break;
        case 0xBB: cp8(E); break;
        case 0xBC: cp8(H); break;
        case 0xBD: cp8(L); break;
        case 0xBE: cp8(memRead(getHL())); base_t = 7; break;
        case 0xBF: cp8(A); break;

        case 0xC0: if (!(F & 0x40)) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xC1: setBC(memRead(SP) | ((uint16_t)memRead(SP + 1) << 8)); SP += 2; base_t = 10; break;
        case 0xC2: { uint16_t a = fetch16(); if (!(F & 0x40)) { PC = a; } base_t = 10; } break;
        case 0xC3: PC = fetch16(); base_t = 10; break;
        case 0xC4: { uint16_t a = fetch16(); if (!(F & 0x40)) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xC5: memWrite(--SP, B); memWrite(--SP, C); base_t = 11; break;
        case 0xC6: A = add8(A, fetch8(), false); base_t = 7; break;
        case 0xC7: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0000; base_t = 11; } break;
        case 0xC8: if (F & 0x40) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xC9: PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 10; break;
        case 0xCA: { uint16_t a = fetch16(); if (F & 0x40) { PC = a; } base_t = 10; } break;
        case 0xCB: op_CB(); return tstates;
        case 0xCC: { uint16_t a = fetch16(); if (F & 0x40) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xCD: { uint16_t a = fetch16(); memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } break;
        case 0xCE: A = add8(A, fetch8(), true); base_t = 7; break;
        case 0xCF: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0008; base_t = 11; } break;

        case 0xD0: if (!(F & 0x01)) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xD1: setDE(memRead(SP) | ((uint16_t)memRead(SP + 1) << 8)); SP += 2; base_t = 10; break;
        case 0xD2: { uint16_t a = fetch16(); if (!(F & 0x01)) { PC = a; } base_t = 10; } break;
        case 0xD3: { uint8_t p = fetch8(); ioWrite(((uint16_t)A << 8) | p, A); base_t = 11; } break;
        case 0xD4: { uint16_t a = fetch16(); if (!(F & 0x01)) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xD5: memWrite(--SP, D); memWrite(--SP, E); base_t = 11; break;
        case 0xD6: A = sub8(A, fetch8(), false); base_t = 7; break;
        case 0xD7: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0010; base_t = 11; } break;
        case 0xD8: if (F & 0x01) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xD9: { uint16_t v = getAF(); setAF(getAF_()); setAF_(v); v = getBC(); setBC(getBC_()); setBC_(v); v = getDE(); setDE(getDE_()); setDE_(v); v = getHL(); setHL(getHL_()); setHL_(v); } break;
        case 0xDA: { uint16_t a = fetch16(); if (F & 0x01) { PC = a; } base_t = 10; } break;
        case 0xDB: { uint8_t p = fetch8(); A = ioRead(((uint16_t)A << 8) | p); base_t = 11; } break;
        case 0xDC: { uint16_t a = fetch16(); if (F & 0x01) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xDD: op_DD(); return tstates;
        case 0xDE: A = sub8(A, fetch8(), true); base_t = 7; break;
        case 0xDF: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0018; base_t = 11; } break;

        case 0xE0: if (!(F & 0x04)) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xE1: setHL(memRead(SP) | ((uint16_t)memRead(SP + 1) << 8)); SP += 2; base_t = 10; break;
        case 0xE2: { uint16_t a = fetch16(); if (!(F & 0x04)) { PC = a; } base_t = 10; } break;
        case 0xE3: { uint16_t v = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); memWrite(SP, L); memWrite(SP + 1, H); setHL(v); base_t = 19; } break;
        case 0xE4: { uint16_t a = fetch16(); if (!(F & 0x04)) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xE5: memWrite(--SP, H); memWrite(--SP, L); base_t = 11; break;
        case 0xE6: and8(fetch8()); base_t = 7; break;
        case 0xE7: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0020; base_t = 11; } break;
        case 0xE8: if (F & 0x04) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xE9: PC = getHL(); break;
        case 0xEA: { uint16_t a = fetch16(); if (F & 0x04) { PC = a; } base_t = 10; } break;
        case 0xEB: { uint16_t v = getDE(); setDE(getHL()); setHL(v); } break;
        case 0xEC: { uint16_t a = fetch16(); if (F & 0x04) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xED: op_ED(); return tstates;
        case 0xEE: xor8(fetch8()); base_t = 7; break;
        case 0xEF: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0028; base_t = 11; } break;

        case 0xF0: if (!(F & 0x80)) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xF1: setAF(memRead(SP) | ((uint16_t)memRead(SP + 1) << 8)); SP += 2; base_t = 10; break;
        case 0xF2: { uint16_t a = fetch16(); if (!(F & 0x80)) { PC = a; } base_t = 10; } break;
        case 0xF3: IFF1 = IFF2 = false; break;
        case 0xF4: { uint16_t a = fetch16(); if (!(F & 0x80)) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xF5: memWrite(--SP, A); memWrite(--SP, F); base_t = 11; break;
        case 0xF6: or8(fetch8()); base_t = 7; break;
        case 0xF7: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0030; base_t = 11; } break;
        case 0xF8: if (F & 0x80) { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 11; } else base_t = 5; break;
        case 0xF9: SP = getHL(); base_t = 6; break;
        case 0xFA: { uint16_t a = fetch16(); if (F & 0x80) { PC = a; } base_t = 10; } break;
        case 0xFB: ei_pending = true; break;
        case 0xFC: { uint16_t a = fetch16(); if (F & 0x80) { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = a; base_t = 17; } else base_t = 10; } break;
        case 0xFD: op_FD(); return tstates;
        case 0xFE: cp8(fetch8()); base_t = 7; break;
        case 0xFF: { memWrite(--SP, (uint8_t)(PC >> 8)); memWrite(--SP, (uint8_t)(PC & 0xFF)); PC = 0x0038; base_t = 11; } break;
    }

    tstates += base_t;
    ula->step(base_t);
    return base_t;
}

void Z80::op_ED() {
    uint8_t op = fetch8();
    int base_t = 4;

    switch (op) {
        case 0x40: B = ioRead(getBC()); F = (F & 0x01); setFlagSZP(B); setFlag53(B); base_t = 12; break;
        case 0x41: ioWrite(getBC(), B); base_t = 12; break;
        case 0x42: { uint16_t v = getHL(); setHL(v - 1); uint8_t r = sub8(A, memRead(getBC()), false); if (F & 0x40) { PC -= 2; } base_t = 16; } break;
        case 0x43: { uint16_t a = fetch16(); memWrite(a, C); memWrite(a + 1, B); base_t = 20; } break;
        case 0x44: A = sub8(A, 0, false); break;
        case 0x45: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; IFF1 = IFF2; base_t = 14; } break;
        case 0x46: IM = 0; base_t = 8; break;
        case 0x47: I = A; base_t = 9; break;
        case 0x48: C = ioRead(getBC()); F = (F & 0x01); setFlagSZP(C); setFlag53(C); base_t = 12; break;
        case 0x49: ioWrite(getBC(), C); base_t = 12; break;
        case 0x4A: { uint16_t v = getHL(); setHL(v - 1); uint8_t r = sub8(A, memRead(getBC()), true); if (F & 0x40) { PC -= 2; } base_t = 16; } break;
        case 0x4B: { uint16_t a = fetch16(); C = memRead(a); B = memRead(a + 1); base_t = 20; } break;
        case 0x4C: A = sub8(A, 0, true); break;
        case 0x4D: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 14; } break;
        case 0x4E: IM = 0; base_t = 8; break;
        case 0x4F: R = A; base_t = 9; break;

        case 0x50: D = ioRead(getBC()); F = (F & 0x01); setFlagSZP(D); setFlag53(D); base_t = 12; break;
        case 0x51: ioWrite(getBC(), D); base_t = 12; break;
        case 0x52: { uint16_t v = getHL(); setHL(v - 1); sub8(A, memRead(getBC()), false); base_t = 16; } break;
        case 0x53: { uint16_t a = fetch16(); memWrite(a, E); memWrite(a + 1, D); base_t = 20; } break;
        case 0x54: A = sub8(A, 0, false); break;
        case 0x55: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; IFF1 = IFF2; base_t = 14; } break;
        case 0x56: IM = 1; base_t = 8; break;
        case 0x57: A = I; F = (F & 0x01); setFlagSZ(A); setFlagPV(IFF2); setFlag53(A); base_t = 9; break;
        case 0x58: E = ioRead(getBC()); F = (F & 0x01); setFlagSZP(E); setFlag53(E); base_t = 12; break;
        case 0x59: ioWrite(getBC(), E); base_t = 12; break;
        case 0x5A: { uint16_t v = getHL(); setHL(v - 1); sub8(A, memRead(getBC()), true); base_t = 16; } break;
        case 0x5B: { uint16_t a = fetch16(); E = memRead(a); D = memRead(a + 1); base_t = 20; } break;
        case 0x5C: A = sub8(A, 0, true); break;
        case 0x5D: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 14; } break;
        case 0x5E: IM = 2; base_t = 8; break;
        case 0x5F: R = A; base_t = 9; break;

        case 0x60: H = ioRead(getBC()); F = (F & 0x01); setFlagSZP(H); setFlag53(H); base_t = 12; break;
        case 0x61: ioWrite(getBC(), H); base_t = 12; break;
        case 0x62: { uint16_t v = getHL(); setHL(v - 1); sub8(A, memRead(getBC()), false); base_t = 16; } break;
        case 0x63: { uint16_t a = fetch16(); memWrite(a, L); memWrite(a + 1, H); base_t = 20; } break;
        case 0x64: A = sub8(A, 0, false); break;
        case 0x65: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; IFF1 = IFF2; base_t = 14; } break;
        case 0x66: IM = 0; base_t = 8; break;
        case 0x67: { // RRD
            uint8_t hl = memRead(getHL());
            uint8_t new_hl = (A << 4) | (hl >> 4);
            A = (A & 0xF0) | (hl & 0x0F);
            memWrite(getHL(), new_hl);
            F = (F & 0x01);
            setFlagSZP(A);
            setFlag53(A);
            base_t = 18;
        } break;
        case 0x68: L = ioRead(getBC()); F = (F & 0x01); setFlagSZP(L); setFlag53(L); base_t = 12; break;
        case 0x69: ioWrite(getBC(), L); base_t = 12; break;
        case 0x6A: { uint16_t v = getHL(); setHL(v - 1); sub8(A, memRead(getBC()), true); base_t = 16; } break;
        case 0x6B: { uint16_t a = fetch16(); L = memRead(a); H = memRead(a + 1); base_t = 20; } break;
        case 0x6C: A = sub8(A, 0, true); break;
        case 0x6D: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 14; } break;
        case 0x6E: IM = 0; base_t = 8; break;
        case 0x6F: { // RLD
            uint8_t hl = memRead(getHL());
            uint8_t new_hl = (hl << 4) | (A & 0x0F);
            A = (A & 0xF0) | (hl >> 4);
            memWrite(getHL(), new_hl);
            F = (F & 0x01);
            setFlagSZP(A);
            setFlag53(A);
            base_t = 18;
        } break;

        case 0x70: { uint8_t v = ioRead(getBC()); F = (F & 0x01); setFlagSZP(v); setFlag53(v); base_t = 12; } break;
        case 0x71: ioWrite(getBC(), 0); base_t = 12; break;
        case 0x72: { uint16_t v = getHL(); setHL(v - 1); uint8_t r = sub8(A, memRead(getBC()), false); if (F & 0x40) { PC -= 2; } base_t = 16; } break;
        case 0x73: { uint16_t a = fetch16(); memWrite(a, (uint8_t)(SP & 0xFF)); memWrite(a + 1, (uint8_t)(SP >> 8)); base_t = 20; } break;
        case 0x74: A = sub8(A, 0, false); break;
        case 0x75: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; IFF1 = IFF2; base_t = 14; } break;
        case 0x76: IM = 1; base_t = 8; break;
        case 0x77: A = I; F = (F & 0x01); setFlagSZ(A); setFlagPV(IFF2); setFlag53(A); base_t = 9; break;
        case 0x78: A = ioRead(getBC()); F = (F & 0x01); setFlagSZP(A); setFlag53(A); base_t = 12; break;
        case 0x79: ioWrite(getBC(), A); base_t = 12; break;
        case 0x7A: { uint16_t v = getHL(); setHL(v - 1); uint8_t r = sub8(A, memRead(getBC()), true); if (F & 0x40) { PC -= 2; } base_t = 16; } break;
        case 0x7B: { uint16_t a = fetch16(); SP = memRead(a) | ((uint16_t)memRead(a + 1) << 8); base_t = 20; } break;
        case 0x7C: A = sub8(A, 0, true); break;
        case 0x7D: { PC = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 14; } break;
        case 0x7E: IM = 2; base_t = 8; break;
        case 0x7F: R = A; base_t = 9; break;

        case 0xA0: { // LDI
            uint8_t v = memRead(getHL());
            memWrite(getDE(), v);
            setDE(getDE() + 1);
            setHL(getHL() + 1);
            setBC(getBC() - 1);
            F = F & 0xC1;
            setFlagPV(getBC() != 0);
            uint8_t n = v + A;
            setFlag53(n);
            base_t = 16;
        } break;
        case 0xA1: { // CPI
            uint8_t v = memRead(getHL());
            uint8_t r = sub8(A, v, false);
            setHL(getHL() + 1);
            setBC(getBC() - 1);
            F = (F & 0x01) | 0x02;
            setFlagPV(getBC() != 0);
            setFlagH((r & 0x0F) > (A & 0x0F));
            setFlag53(r);
            base_t = 16;
        } break;
        case 0xA2: { // INI
            uint8_t v = ioRead(getBC());
            memWrite(getHL(), v);
            setHL(getHL() + 1);
            B = dec8(B);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            base_t = 16;
        } break;
        case 0xA3: { // OUTI
            uint8_t v = memRead(getHL());
            setHL(getHL() + 1);
            B = dec8(B);
            ioWrite(getBC(), v);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            base_t = 16;
        } break;
        case 0xA8: { // LDD
            uint8_t v = memRead(getHL());
            memWrite(getDE(), v);
            setDE(getDE() - 1);
            setHL(getHL() - 1);
            setBC(getBC() - 1);
            F = F & 0xC1;
            setFlagPV(getBC() != 0);
            uint8_t n = v + A;
            setFlag53(n);
            base_t = 16;
        } break;
        case 0xA9: { // CPD
            uint8_t v = memRead(getHL());
            uint8_t r = sub8(A, v, false);
            setHL(getHL() - 1);
            setBC(getBC() - 1);
            F = (F & 0x01) | 0x02;
            setFlagPV(getBC() != 0);
            setFlagH((r & 0x0F) > (A & 0x0F));
            setFlag53(r);
            base_t = 16;
        } break;
        case 0xAA: { // IND
            uint8_t v = ioRead(getBC());
            memWrite(getHL(), v);
            setHL(getHL() - 1);
            B = dec8(B);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            base_t = 16;
        } break;
        case 0xAB: { // OUTD
            uint8_t v = memRead(getHL());
            setHL(getHL() - 1);
            B = dec8(B);
            ioWrite(getBC(), v);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            base_t = 16;
        } break;
        case 0xB0: { // LDIR
            uint8_t v = memRead(getHL());
            memWrite(getDE(), v);
            setDE(getDE() + 1);
            setHL(getHL() + 1);
            setBC(getBC() - 1);
            F = F & 0xC1;
            setFlagPV(false);
            if (getBC() != 0) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;
        case 0xB1: { // CPIR
            uint8_t v = memRead(getHL());
            uint8_t r = sub8(A, v, false);
            setHL(getHL() + 1);
            setBC(getBC() - 1);
            F = (F & 0x01) | 0x02;
            setFlagPV(getBC() != 0);
            setFlagH((r & 0x0F) > (A & 0x0F));
            setFlag53(r);
            if (getBC() != 0 && !(F & 0x40)) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;
        case 0xB2: { // INIR
            uint8_t v = ioRead(getBC());
            memWrite(getHL(), v);
            setHL(getHL() + 1);
            B = dec8(B);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            if (B != 0) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;
        case 0xB3: { // OTIR
            uint8_t v = memRead(getHL());
            setHL(getHL() + 1);
            B = dec8(B);
            ioWrite(getBC(), v);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            if (B != 0) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;
        case 0xB8: { // LDDR
            uint8_t v = memRead(getHL());
            memWrite(getDE(), v);
            setDE(getDE() - 1);
            setHL(getHL() - 1);
            setBC(getBC() - 1);
            F = F & 0xC1;
            setFlagPV(false);
            if (getBC() != 0) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;
        case 0xB9: { // CPDR
            uint8_t v = memRead(getHL());
            uint8_t r = sub8(A, v, false);
            setHL(getHL() - 1);
            setBC(getBC() - 1);
            F = (F & 0x01) | 0x02;
            setFlagPV(getBC() != 0);
            setFlagH((r & 0x0F) > (A & 0x0F));
            setFlag53(r);
            if (getBC() != 0 && !(F & 0x40)) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;
        case 0xBA: { // INDR
            uint8_t v = ioRead(getBC());
            memWrite(getHL(), v);
            setHL(getHL() - 1);
            B = dec8(B);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            if (B != 0) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;
        case 0xBB: { // OTDR
            uint8_t v = memRead(getHL());
            setHL(getHL() - 1);
            B = dec8(B);
            ioWrite(getBC(), v);
            F = (F & 0x01);
            setFlagZ(B);
            setFlagN(true);
            if (B != 0) { PC -= 2; base_t = 21; } else base_t = 16;
        } break;

        default: break;
    }

    tstates += base_t;
    ula->step(base_t);
}

void Z80::op_CB() {
    uint8_t op = fetch8();
    int base_t = 8;

    switch (op) {
        case 0x00: B = rlc(B); break;
        case 0x01: C = rlc(C); break;
        case 0x02: D = rlc(D); break;
        case 0x03: E = rlc(E); break;
        case 0x04: H = rlc(H); break;
        case 0x05: L = rlc(L); break;
        case 0x06: { uint8_t v = rlc(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x07: A = rlc(A); break;
        case 0x08: B = rrc(B); break;
        case 0x09: C = rrc(C); break;
        case 0x0A: D = rrc(D); break;
        case 0x0B: E = rrc(E); break;
        case 0x0C: H = rrc(H); break;
        case 0x0D: L = rrc(L); break;
        case 0x0E: { uint8_t v = rrc(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x0F: A = rrc(A); break;

        case 0x10: B = rl(B); break;
        case 0x11: C = rl(C); break;
        case 0x12: D = rl(D); break;
        case 0x13: E = rl(E); break;
        case 0x14: H = rl(H); break;
        case 0x15: L = rl(L); break;
        case 0x16: { uint8_t v = rl(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x17: A = rl(A); break;
        case 0x18: B = rr(B); break;
        case 0x19: C = rr(C); break;
        case 0x1A: D = rr(D); break;
        case 0x1B: E = rr(E); break;
        case 0x1C: H = rr(H); break;
        case 0x1D: L = rr(L); break;
        case 0x1E: { uint8_t v = rr(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x1F: A = rr(A); break;

        case 0x20: B = sla(B); break;
        case 0x21: C = sla(C); break;
        case 0x22: D = sla(D); break;
        case 0x23: E = sla(E); break;
        case 0x24: H = sla(H); break;
        case 0x25: L = sla(L); break;
        case 0x26: { uint8_t v = sla(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x27: A = sla(A); break;
        case 0x28: B = sra(B); break;
        case 0x29: C = sra(C); break;
        case 0x2A: D = sra(D); break;
        case 0x2B: E = sra(E); break;
        case 0x2C: H = sra(H); break;
        case 0x2D: L = sra(L); break;
        case 0x2E: { uint8_t v = sra(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x2F: A = sra(A); break;

        case 0x30: B = sll(B); break;
        case 0x31: C = sll(C); break;
        case 0x32: D = sll(D); break;
        case 0x33: E = sll(E); break;
        case 0x34: H = sll(H); break;
        case 0x35: L = sll(L); break;
        case 0x36: { uint8_t v = sll(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x37: A = sll(A); break;
        case 0x38: B = srl(B); break;
        case 0x39: C = srl(C); break;
        case 0x3A: D = srl(D); break;
        case 0x3B: E = srl(E); break;
        case 0x3C: H = srl(H); break;
        case 0x3D: L = srl(L); break;
        case 0x3E: { uint8_t v = srl(memRead(getHL())); memWrite(getHL(), v); base_t = 15; } break;
        case 0x3F: A = srl(A); break;

        case 0x40: bit(0, B); break;
        case 0x41: bit(0, C); break;
        case 0x42: bit(0, D); break;
        case 0x43: bit(0, E); break;
        case 0x44: bit(0, H); break;
        case 0x45: bit(0, L); break;
        case 0x46: bit(0, memRead(getHL())); base_t = 12; break;
        case 0x47: bit(0, A); break;
        case 0x48: bit(1, B); break;
        case 0x49: bit(1, C); break;
        case 0x4A: bit(1, D); break;
        case 0x4B: bit(1, E); break;
        case 0x4C: bit(1, H); break;
        case 0x4D: bit(1, L); break;
        case 0x4E: bit(1, memRead(getHL())); base_t = 12; break;
        case 0x4F: bit(1, A); break;

        case 0x50: bit(2, B); break;
        case 0x51: bit(2, C); break;
        case 0x52: bit(2, D); break;
        case 0x53: bit(2, E); break;
        case 0x54: bit(2, H); break;
        case 0x55: bit(2, L); break;
        case 0x56: bit(2, memRead(getHL())); base_t = 12; break;
        case 0x57: bit(2, A); break;
        case 0x58: bit(3, B); break;
        case 0x59: bit(3, C); break;
        case 0x5A: bit(3, D); break;
        case 0x5B: bit(3, E); break;
        case 0x5C: bit(3, H); break;
        case 0x5D: bit(3, L); break;
        case 0x5E: bit(3, memRead(getHL())); base_t = 12; break;
        case 0x5F: bit(3, A); break;

        case 0x60: bit(4, B); break;
        case 0x61: bit(4, C); break;
        case 0x62: bit(4, D); break;
        case 0x63: bit(4, E); break;
        case 0x64: bit(4, H); break;
        case 0x65: bit(4, L); break;
        case 0x66: bit(4, memRead(getHL())); base_t = 12; break;
        case 0x67: bit(4, A); break;
        case 0x68: bit(5, B); break;
        case 0x69: bit(5, C); break;
        case 0x6A: bit(5, D); break;
        case 0x6B: bit(5, E); break;
        case 0x6C: bit(5, H); break;
        case 0x6D: bit(5, L); break;
        case 0x6E: bit(5, memRead(getHL())); base_t = 12; break;
        case 0x6F: bit(5, A); break;

        case 0x70: bit(6, B); break;
        case 0x71: bit(6, C); break;
        case 0x72: bit(6, D); break;
        case 0x73: bit(6, E); break;
        case 0x74: bit(6, H); break;
        case 0x75: bit(6, L); break;
        case 0x76: bit(6, memRead(getHL())); base_t = 12; break;
        case 0x77: bit(6, A); break;
        case 0x78: bit(7, B); break;
        case 0x79: bit(7, C); break;
        case 0x7A: bit(7, D); break;
        case 0x7B: bit(7, E); break;
        case 0x7C: bit(7, H); break;
        case 0x7D: bit(7, L); break;
        case 0x7E: bit(7, memRead(getHL())); base_t = 12; break;
        case 0x7F: bit(7, A); break;

        case 0x80: B &= ~(1 << 0); break;
        case 0x81: C &= ~(1 << 0); break;
        case 0x82: D &= ~(1 << 0); break;
        case 0x83: E &= ~(1 << 0); break;
        case 0x84: H &= ~(1 << 0); break;
        case 0x85: L &= ~(1 << 0); break;
        case 0x86: { uint8_t v = memRead(getHL()) & ~(1 << 0); memWrite(getHL(), v); base_t = 15; } break;
        case 0x87: A &= ~(1 << 0); break;
        case 0x88: B &= ~(1 << 1); break;
        case 0x89: C &= ~(1 << 1); break;
        case 0x8A: D &= ~(1 << 1); break;
        case 0x8B: E &= ~(1 << 1); break;
        case 0x8C: H &= ~(1 << 1); break;
        case 0x8D: L &= ~(1 << 1); break;
        case 0x8E: { uint8_t v = memRead(getHL()) & ~(1 << 1); memWrite(getHL(), v); base_t = 15; } break;
        case 0x8F: A &= ~(1 << 1); break;

        case 0x90: B &= ~(1 << 2); break;
        case 0x91: C &= ~(1 << 2); break;
        case 0x92: D &= ~(1 << 2); break;
        case 0x93: E &= ~(1 << 2); break;
        case 0x94: H &= ~(1 << 2); break;
        case 0x95: L &= ~(1 << 2); break;
        case 0x96: { uint8_t v = memRead(getHL()) & ~(1 << 2); memWrite(getHL(), v); base_t = 15; } break;
        case 0x97: A &= ~(1 << 2); break;
        case 0x98: B &= ~(1 << 3); break;
        case 0x99: C &= ~(1 << 3); break;
        case 0x9A: D &= ~(1 << 3); break;
        case 0x9B: E &= ~(1 << 3); break;
        case 0x9C: H &= ~(1 << 3); break;
        case 0x9D: L &= ~(1 << 3); break;
        case 0x9E: { uint8_t v = memRead(getHL()) & ~(1 << 3); memWrite(getHL(), v); base_t = 15; } break;
        case 0x9F: A &= ~(1 << 3); break;

        case 0xA0: B &= ~(1 << 4); break;
        case 0xA1: C &= ~(1 << 4); break;
        case 0xA2: D &= ~(1 << 4); break;
        case 0xA3: E &= ~(1 << 4); break;
        case 0xA4: H &= ~(1 << 4); break;
        case 0xA5: L &= ~(1 << 4); break;
        case 0xA6: { uint8_t v = memRead(getHL()) & ~(1 << 4); memWrite(getHL(), v); base_t = 15; } break;
        case 0xA7: A &= ~(1 << 4); break;
        case 0xA8: B &= ~(1 << 5); break;
        case 0xA9: C &= ~(1 << 5); break;
        case 0xAA: D &= ~(1 << 5); break;
        case 0xAB: E &= ~(1 << 5); break;
        case 0xAC: H &= ~(1 << 5); break;
        case 0xAD: L &= ~(1 << 5); break;
        case 0xAE: { uint8_t v = memRead(getHL()) & ~(1 << 5); memWrite(getHL(), v); base_t = 15; } break;
        case 0xAF: A &= ~(1 << 5); break;

        case 0xB0: B &= ~(1 << 6); break;
        case 0xB1: C &= ~(1 << 6); break;
        case 0xB2: D &= ~(1 << 6); break;
        case 0xB3: E &= ~(1 << 6); break;
        case 0xB4: H &= ~(1 << 6); break;
        case 0xB5: L &= ~(1 << 6); break;
        case 0xB6: { uint8_t v = memRead(getHL()) & ~(1 << 6); memWrite(getHL(), v); base_t = 15; } break;
        case 0xB7: A &= ~(1 << 6); break;
        case 0xB8: B &= ~(1 << 7); break;
        case 0xB9: C &= ~(1 << 7); break;
        case 0xBA: D &= ~(1 << 7); break;
        case 0xBB: E &= ~(1 << 7); break;
        case 0xBC: H &= ~(1 << 7); break;
        case 0xBD: L &= ~(1 << 7); break;
        case 0xBE: { uint8_t v = memRead(getHL()) & ~(1 << 7); memWrite(getHL(), v); base_t = 15; } break;
        case 0xBF: A &= ~(1 << 7); break;

        case 0xC0: B |= (1 << 0); break;
        case 0xC1: C |= (1 << 0); break;
        case 0xC2: D |= (1 << 0); break;
        case 0xC3: E |= (1 << 0); break;
        case 0xC4: H |= (1 << 0); break;
        case 0xC5: L |= (1 << 0); break;
        case 0xC6: { uint8_t v = memRead(getHL()) | (1 << 0); memWrite(getHL(), v); base_t = 15; } break;
        case 0xC7: A |= (1 << 0); break;
        case 0xC8: B |= (1 << 1); break;
        case 0xC9: C |= (1 << 1); break;
        case 0xCA: D |= (1 << 1); break;
        case 0xCB: E |= (1 << 1); break;
        case 0xCC: H |= (1 << 1); break;
        case 0xCD: L |= (1 << 1); break;
        case 0xCE: { uint8_t v = memRead(getHL()) | (1 << 1); memWrite(getHL(), v); base_t = 15; } break;
        case 0xCF: A |= (1 << 1); break;

        case 0xD0: B |= (1 << 2); break;
        case 0xD1: C |= (1 << 2); break;
        case 0xD2: D |= (1 << 2); break;
        case 0xD3: E |= (1 << 2); break;
        case 0xD4: H |= (1 << 2); break;
        case 0xD5: L |= (1 << 2); break;
        case 0xD6: { uint8_t v = memRead(getHL()) | (1 << 2); memWrite(getHL(), v); base_t = 15; } break;
        case 0xD7: A |= (1 << 2); break;
        case 0xD8: B |= (1 << 3); break;
        case 0xD9: C |= (1 << 3); break;
        case 0xDA: D |= (1 << 3); break;
        case 0xDB: E |= (1 << 3); break;
        case 0xDC: H |= (1 << 3); break;
        case 0xDD: L |= (1 << 3); break;
        case 0xDE: { uint8_t v = memRead(getHL()) | (1 << 3); memWrite(getHL(), v); base_t = 15; } break;
        case 0xDF: A |= (1 << 3); break;

        case 0xE0: B |= (1 << 4); break;
        case 0xE1: C |= (1 << 4); break;
        case 0xE2: D |= (1 << 4); break;
        case 0xE3: E |= (1 << 4); break;
        case 0xE4: H |= (1 << 4); break;
        case 0xE5: L |= (1 << 4); break;
        case 0xE6: { uint8_t v = memRead(getHL()) | (1 << 4); memWrite(getHL(), v); base_t = 15; } break;
        case 0xE7: A |= (1 << 4); break;
        case 0xE8: B |= (1 << 5); break;
        case 0xE9: C |= (1 << 5); break;
        case 0xEA: D |= (1 << 5); break;
        case 0xEB: E |= (1 << 5); break;
        case 0xEC: H |= (1 << 5); break;
        case 0xED: L |= (1 << 5); break;
        case 0xEE: { uint8_t v = memRead(getHL()) | (1 << 5); memWrite(getHL(), v); base_t = 15; } break;
        case 0xEF: A |= (1 << 5); break;

        case 0xF0: B |= (1 << 6); break;
        case 0xF1: C |= (1 << 6); break;
        case 0xF2: D |= (1 << 6); break;
        case 0xF3: E |= (1 << 6); break;
        case 0xF4: H |= (1 << 6); break;
        case 0xF5: L |= (1 << 6); break;
        case 0xF6: { uint8_t v = memRead(getHL()) | (1 << 6); memWrite(getHL(), v); base_t = 15; } break;
        case 0xF7: A |= (1 << 6); break;
        case 0xF8: B |= (1 << 7); break;
        case 0xF9: C |= (1 << 7); break;
        case 0xFA: D |= (1 << 7); break;
        case 0xFB: E |= (1 << 7); break;
        case 0xFC: H |= (1 << 7); break;
        case 0xFD: L |= (1 << 7); break;
        case 0xFE: { uint8_t v = memRead(getHL()) | (1 << 7); memWrite(getHL(), v); base_t = 15; } break;
        case 0xFF: A |= (1 << 7); break;
    }

    tstates += base_t;
    ula->step(base_t);
}

void Z80::op_DD() {
    uint8_t op = fetch8();
    int base_t = 4;

    if (op == 0xCB) {
        uint8_t d = fetch8();
        op_DDCB(d);
        return;
    }

    switch (op) {
        case 0x09: setIX(add16(IX, getBC())); base_t = 15; break;
        case 0x19: setIX(add16(IX, getDE())); base_t = 15; break;
        case 0x21: IX = fetch16(); base_t = 14; break;
        case 0x22: { uint16_t a = fetch16(); memWrite(a, (uint8_t)(IX & 0xFF)); memWrite(a + 1, (uint8_t)(IX >> 8)); base_t = 20; } break;
        case 0x23: IX++; base_t = 10; break;
        case 0x29: setIX(add16(IX, IX)); base_t = 15; break;
        case 0x2A: { uint16_t a = fetch16(); IX = memRead(a) | ((uint16_t)memRead(a + 1) << 8); base_t = 20; } break;
        case 0x2B: IX--; base_t = 10; break;
        case 0x34: { int8_t d = (int8_t)fetch8(); uint16_t a = IX + d; uint8_t v = inc8(memRead(a)); memWrite(a, v); base_t = 23; } break;
        case 0x35: { int8_t d = (int8_t)fetch8(); uint16_t a = IX + d; uint8_t v = dec8(memRead(a)); memWrite(a, v); base_t = 23; } break;
        case 0x36: { int8_t d = (int8_t)fetch8(); uint8_t v = fetch8(); memWrite(IX + d, v); base_t = 19; } break;
        case 0x39: setIX(add16(IX, SP)); base_t = 15; break;
        case 0x46: { int8_t d = (int8_t)fetch8(); B = memRead(IX + d); base_t = 19; } break;
        case 0x4E: { int8_t d = (int8_t)fetch8(); C = memRead(IX + d); base_t = 19; } break;
        case 0x56: { int8_t d = (int8_t)fetch8(); D = memRead(IX + d); base_t = 19; } break;
        case 0x5E: { int8_t d = (int8_t)fetch8(); E = memRead(IX + d); base_t = 19; } break;
        case 0x66: { int8_t d = (int8_t)fetch8(); H = memRead(IX + d); base_t = 19; } break;
        case 0x6E: { int8_t d = (int8_t)fetch8(); L = memRead(IX + d); base_t = 19; } break;
        case 0x70: { int8_t d = (int8_t)fetch8(); memWrite(IX + d, B); base_t = 19; } break;
        case 0x71: { int8_t d = (int8_t)fetch8(); memWrite(IX + d, C); base_t = 19; } break;
        case 0x72: { int8_t d = (int8_t)fetch8(); memWrite(IX + d, D); base_t = 19; } break;
        case 0x73: { int8_t d = (int8_t)fetch8(); memWrite(IX + d, E); base_t = 19; } break;
        case 0x74: { int8_t d = (int8_t)fetch8(); memWrite(IX + d, H); base_t = 19; } break;
        case 0x75: { int8_t d = (int8_t)fetch8(); memWrite(IX + d, L); base_t = 19; } break;
        case 0x77: { int8_t d = (int8_t)fetch8(); memWrite(IX + d, A); base_t = 19; } break;
        case 0x7E: { int8_t d = (int8_t)fetch8(); A = memRead(IX + d); base_t = 19; } break;
        case 0x86: { int8_t d = (int8_t)fetch8(); A = add8(A, memRead(IX + d), false); base_t = 19; } break;
        case 0x8E: { int8_t d = (int8_t)fetch8(); A = add8(A, memRead(IX + d), true); base_t = 19; } break;
        case 0x96: { int8_t d = (int8_t)fetch8(); A = sub8(A, memRead(IX + d), false); base_t = 19; } break;
        case 0x9E: { int8_t d = (int8_t)fetch8(); A = sub8(A, memRead(IX + d), true); base_t = 19; } break;
        case 0xA6: { int8_t d = (int8_t)fetch8(); and8(memRead(IX + d)); base_t = 19; } break;
        case 0xAE: { int8_t d = (int8_t)fetch8(); xor8(memRead(IX + d)); base_t = 19; } break;
        case 0xB6: { int8_t d = (int8_t)fetch8(); or8(memRead(IX + d)); base_t = 19; } break;
        case 0xBE: { int8_t d = (int8_t)fetch8(); cp8(memRead(IX + d)); base_t = 19; } break;
        case 0xE1: IX = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 14; break;
        case 0xE3: { uint16_t v = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); memWrite(SP, (uint8_t)(IX & 0xFF)); memWrite(SP + 1, (uint8_t)(IX >> 8)); IX = v; base_t = 23; } break;
        case 0xE5: memWrite(--SP, (uint8_t)(IX >> 8)); memWrite(--SP, (uint8_t)(IX & 0xFF)); base_t = 15; break;
        case 0xE9: PC = IX; base_t = 8; break;
        case 0xF9: SP = IX; base_t = 10; break;
        default: {
            PC--;
            execute();
            return;
        }
    }

    tstates += base_t;
    ula->step(base_t);
}

void Z80::op_FD() {
    uint8_t op = fetch8();
    int base_t = 4;

    if (op == 0xCB) {
        uint8_t d = fetch8();
        op_FDCB(d);
        return;
    }

    switch (op) {
        case 0x09: setIY(add16(IY, getBC())); base_t = 15; break;
        case 0x19: setIY(add16(IY, getDE())); base_t = 15; break;
        case 0x21: IY = fetch16(); base_t = 14; break;
        case 0x22: { uint16_t a = fetch16(); memWrite(a, (uint8_t)(IY & 0xFF)); memWrite(a + 1, (uint8_t)(IY >> 8)); base_t = 20; } break;
        case 0x23: IY++; base_t = 10; break;
        case 0x29: setIY(add16(IY, IY)); base_t = 15; break;
        case 0x2A: { uint16_t a = fetch16(); IY = memRead(a) | ((uint16_t)memRead(a + 1) << 8); base_t = 20; } break;
        case 0x2B: IY--; base_t = 10; break;
        case 0x34: { int8_t d = (int8_t)fetch8(); uint16_t a = IY + d; uint8_t v = inc8(memRead(a)); memWrite(a, v); base_t = 23; } break;
        case 0x35: { int8_t d = (int8_t)fetch8(); uint16_t a = IY + d; uint8_t v = dec8(memRead(a)); memWrite(a, v); base_t = 23; } break;
        case 0x36: { int8_t d = (int8_t)fetch8(); uint8_t v = fetch8(); memWrite(IY + d, v); base_t = 19; } break;
        case 0x39: setIY(add16(IY, SP)); base_t = 15; break;
        case 0x46: { int8_t d = (int8_t)fetch8(); B = memRead(IY + d); base_t = 19; } break;
        case 0x4E: { int8_t d = (int8_t)fetch8(); C = memRead(IY + d); base_t = 19; } break;
        case 0x56: { int8_t d = (int8_t)fetch8(); D = memRead(IY + d); base_t = 19; } break;
        case 0x5E: { int8_t d = (int8_t)fetch8(); E = memRead(IY + d); base_t = 19; } break;
        case 0x66: { int8_t d = (int8_t)fetch8(); H = memRead(IY + d); base_t = 19; } break;
        case 0x6E: { int8_t d = (int8_t)fetch8(); L = memRead(IY + d); base_t = 19; } break;
        case 0x70: { int8_t d = (int8_t)fetch8(); memWrite(IY + d, B); base_t = 19; } break;
        case 0x71: { int8_t d = (int8_t)fetch8(); memWrite(IY + d, C); base_t = 19; } break;
        case 0x72: { int8_t d = (int8_t)fetch8(); memWrite(IY + d, D); base_t = 19; } break;
        case 0x73: { int8_t d = (int8_t)fetch8(); memWrite(IY + d, E); base_t = 19; } break;
        case 0x74: { int8_t d = (int8_t)fetch8(); memWrite(IY + d, H); base_t = 19; } break;
        case 0x75: { int8_t d = (int8_t)fetch8(); memWrite(IY + d, L); base_t = 19; } break;
        case 0x77: { int8_t d = (int8_t)fetch8(); memWrite(IY + d, A); base_t = 19; } break;
        case 0x7E: { int8_t d = (int8_t)fetch8(); A = memRead(IY + d); base_t = 19; } break;
        case 0x86: { int8_t d = (int8_t)fetch8(); A = add8(A, memRead(IY + d), false); base_t = 19; } break;
        case 0x8E: { int8_t d = (int8_t)fetch8(); A = add8(A, memRead(IY + d), true); base_t = 19; } break;
        case 0x96: { int8_t d = (int8_t)fetch8(); A = sub8(A, memRead(IY + d), false); base_t = 19; } break;
        case 0x9E: { int8_t d = (int8_t)fetch8(); A = sub8(A, memRead(IY + d), true); base_t = 19; } break;
        case 0xA6: { int8_t d = (int8_t)fetch8(); and8(memRead(IY + d)); base_t = 19; } break;
        case 0xAE: { int8_t d = (int8_t)fetch8(); xor8(memRead(IY + d)); base_t = 19; } break;
        case 0xB6: { int8_t d = (int8_t)fetch8(); or8(memRead(IY + d)); base_t = 19; } break;
        case 0xBE: { int8_t d = (int8_t)fetch8(); cp8(memRead(IY + d)); base_t = 19; } break;
        case 0xE1: IY = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); SP += 2; base_t = 14; break;
        case 0xE3: { uint16_t v = memRead(SP) | ((uint16_t)memRead(SP + 1) << 8); memWrite(SP, (uint8_t)(IY & 0xFF)); memWrite(SP + 1, (uint8_t)(IY >> 8)); IY = v; base_t = 23; } break;
        case 0xE5: memWrite(--SP, (uint8_t)(IY >> 8)); memWrite(--SP, (uint8_t)(IY & 0xFF)); base_t = 15; break;
        case 0xE9: PC = IY; base_t = 8; break;
        case 0xF9: SP = IY; base_t = 10; break;
        default: {
            PC--;
            execute();
            return;
        }
    }

    tstates += base_t;
    ula->step(base_t);
}

void Z80::op_DDCB(uint8_t d) {
    uint8_t op = fetch8();
    uint16_t addr = IX + (int8_t)d;
    int base_t = 23;

    switch (op) {
        case 0x06: { uint8_t v = rlc(memRead(addr)); memWrite(addr, v); } break;
        case 0x0E: { uint8_t v = rrc(memRead(addr)); memWrite(addr, v); } break;
        case 0x16: { uint8_t v = rl(memRead(addr)); memWrite(addr, v); } break;
        case 0x1E: { uint8_t v = rr(memRead(addr)); memWrite(addr, v); } break;
        case 0x26: { uint8_t v = sla(memRead(addr)); memWrite(addr, v); } break;
        case 0x2E: { uint8_t v = sra(memRead(addr)); memWrite(addr, v); } break;
        case 0x36: { uint8_t v = sll(memRead(addr)); memWrite(addr, v); } break;
        case 0x3E: { uint8_t v = srl(memRead(addr)); memWrite(addr, v); } break;
        case 0x46: bit(0, memRead(addr)); break;
        case 0x4E: bit(1, memRead(addr)); break;
        case 0x56: bit(2, memRead(addr)); break;
        case 0x5E: bit(3, memRead(addr)); break;
        case 0x66: bit(4, memRead(addr)); break;
        case 0x6E: bit(5, memRead(addr)); break;
        case 0x76: bit(6, memRead(addr)); break;
        case 0x7E: bit(7, memRead(addr)); break;
        case 0x86: { uint8_t v = memRead(addr) & ~(1 << 0); memWrite(addr, v); } break;
        case 0x8E: { uint8_t v = memRead(addr) & ~(1 << 1); memWrite(addr, v); } break;
        case 0x96: { uint8_t v = memRead(addr) & ~(1 << 2); memWrite(addr, v); } break;
        case 0x9E: { uint8_t v = memRead(addr) & ~(1 << 3); memWrite(addr, v); } break;
        case 0xA6: { uint8_t v = memRead(addr) & ~(1 << 4); memWrite(addr, v); } break;
        case 0xAE: { uint8_t v = memRead(addr) & ~(1 << 5); memWrite(addr, v); } break;
        case 0xB6: { uint8_t v = memRead(addr) & ~(1 << 6); memWrite(addr, v); } break;
        case 0xBE: { uint8_t v = memRead(addr) & ~(1 << 7); memWrite(addr, v); } break;
        case 0xC6: { uint8_t v = memRead(addr) | (1 << 0); memWrite(addr, v); } break;
        case 0xCE: { uint8_t v = memRead(addr) | (1 << 1); memWrite(addr, v); } break;
        case 0xD6: { uint8_t v = memRead(addr) | (1 << 2); memWrite(addr, v); } break;
        case 0xDE: { uint8_t v = memRead(addr) | (1 << 3); memWrite(addr, v); } break;
        case 0xE6: { uint8_t v = memRead(addr) | (1 << 4); memWrite(addr, v); } break;
        case 0xEE: { uint8_t v = memRead(addr) | (1 << 5); memWrite(addr, v); } break;
        case 0xF6: { uint8_t v = memRead(addr) | (1 << 6); memWrite(addr, v); } break;
        case 0xFE: { uint8_t v = memRead(addr) | (1 << 7); memWrite(addr, v); } break;
        default: break;
    }

    tstates += base_t;
    ula->step(base_t);
}

void Z80::op_FDCB(uint8_t d) {
    uint8_t op = fetch8();
    uint16_t addr = IY + (int8_t)d;
    int base_t = 23;

    switch (op) {
        case 0x06: { uint8_t v = rlc(memRead(addr)); memWrite(addr, v); } break;
        case 0x0E: { uint8_t v = rrc(memRead(addr)); memWrite(addr, v); } break;
        case 0x16: { uint8_t v = rl(memRead(addr)); memWrite(addr, v); } break;
        case 0x1E: { uint8_t v = rr(memRead(addr)); memWrite(addr, v); } break;
        case 0x26: { uint8_t v = sla(memRead(addr)); memWrite(addr, v); } break;
        case 0x2E: { uint8_t v = sra(memRead(addr)); memWrite(addr, v); } break;
        case 0x36: { uint8_t v = sll(memRead(addr)); memWrite(addr, v); } break;
        case 0x3E: { uint8_t v = srl(memRead(addr)); memWrite(addr, v); } break;
        case 0x46: bit(0, memRead(addr)); break;
        case 0x4E: bit(1, memRead(addr)); break;
        case 0x56: bit(2, memRead(addr)); break;
        case 0x5E: bit(3, memRead(addr)); break;
        case 0x66: bit(4, memRead(addr)); break;
        case 0x6E: bit(5, memRead(addr)); break;
        case 0x76: bit(6, memRead(addr)); break;
        case 0x7E: bit(7, memRead(addr)); break;
        case 0x86: { uint8_t v = memRead(addr) & ~(1 << 0); memWrite(addr, v); } break;
        case 0x8E: { uint8_t v = memRead(addr) & ~(1 << 1); memWrite(addr, v); } break;
        case 0x96: { uint8_t v = memRead(addr) & ~(1 << 2); memWrite(addr, v); } break;
        case 0x9E: { uint8_t v = memRead(addr) & ~(1 << 3); memWrite(addr, v); } break;
        case 0xA6: { uint8_t v = memRead(addr) & ~(1 << 4); memWrite(addr, v); } break;
        case 0xAE: { uint8_t v = memRead(addr) & ~(1 << 5); memWrite(addr, v); } break;
        case 0xB6: { uint8_t v = memRead(addr) & ~(1 << 6); memWrite(addr, v); } break;
        case 0xBE: { uint8_t v = memRead(addr) & ~(1 << 7); memWrite(addr, v); } break;
        case 0xC6: { uint8_t v = memRead(addr) | (1 << 0); memWrite(addr, v); } break;
        case 0xCE: { uint8_t v = memRead(addr) | (1 << 1); memWrite(addr, v); } break;
        case 0xD6: { uint8_t v = memRead(addr) | (1 << 2); memWrite(addr, v); } break;
        case 0xDE: { uint8_t v = memRead(addr) | (1 << 3); memWrite(addr, v); } break;
        case 0xE6: { uint8_t v = memRead(addr) | (1 << 4); memWrite(addr, v); } break;
        case 0xEE: { uint8_t v = memRead(addr) | (1 << 5); memWrite(addr, v); } break;
        case 0xF6: { uint8_t v = memRead(addr) | (1 << 6); memWrite(addr, v); } break;
        case 0xFE: { uint8_t v = memRead(addr) | (1 << 7); memWrite(addr, v); } break;
        default: break;
    }

    tstates += base_t;
    ula->step(base_t);
}
