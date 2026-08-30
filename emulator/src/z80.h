#pragma once
#include <cstdint>

class ULA;

class Z80 {
public:
    uint8_t A, F, B, C, D, E, H, L;
    uint8_t A_, F_, B_, C_, D_, E_, H_, L_;
    uint16_t IX, IY, SP, PC;
    uint8_t I, R;
    bool IFF1, IFF2;
    uint8_t IM;
    bool halted;
    bool ei_pending;
    bool nmi_pending;

    ULA* ula;
    int tstates;

    Z80();
    void reset();

    uint16_t getAF() const { return ((uint16_t)A << 8) | F; }
    uint16_t getBC() const { return ((uint16_t)B << 8) | C; }
    uint16_t getDE() const { return ((uint16_t)D << 8) | E; }
    uint16_t getHL() const { return ((uint16_t)H << 8) | L; }
    void setAF(uint16_t v) { A = (uint8_t)(v >> 8); F = (uint8_t)(v & 0xFF); }
    void setBC(uint16_t v) { B = (uint8_t)(v >> 8); C = (uint8_t)(v & 0xFF); }
    void setDE(uint16_t v) { D = (uint8_t)(v >> 8); E = (uint8_t)(v & 0xFF); }
    void setHL(uint16_t v) { H = (uint8_t)(v >> 8); L = (uint8_t)(v & 0xFF); }
    void setIX(uint16_t v) { IX = v; }
    void setIY(uint16_t v) { IY = v; }

    uint16_t getAF_() const { return ((uint16_t)A_ << 8) | F_; }
    uint16_t getBC_() const { return ((uint16_t)B_ << 8) | C_; }
    uint16_t getDE_() const { return ((uint16_t)D_ << 8) | E_; }
    uint16_t getHL_() const { return ((uint16_t)H_ << 8) | L_; }
    void setAF_(uint16_t v) { A_ = (uint8_t)(v >> 8); F_ = (uint8_t)(v & 0xFF); }
    void setBC_(uint16_t v) { B_ = (uint8_t)(v >> 8); C_ = (uint8_t)(v & 0xFF); }
    void setDE_(uint16_t v) { D_ = (uint8_t)(v >> 8); E_ = (uint8_t)(v & 0xFF); }
    void setHL_(uint16_t v) { H_ = (uint8_t)(v >> 8); L_ = (uint8_t)(v & 0xFF); }

    uint8_t memRead(uint16_t addr);
    void memWrite(uint16_t addr, uint8_t val);
    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t val);

    uint8_t fetch8();
    uint16_t fetch16();

    void setFlagS(uint8_t v) { F = (F & 0x7F) | (v & 0x80); }
    void setFlagZ(uint8_t v) { if (v == 0) F |= 0x40; else F &= ~0x40; }
    void setFlagH(bool h) { if (h) F |= 0x10; else F &= ~0x10; }
    void setFlagPV(bool pv) { if (pv) F |= 0x04; else F &= ~0x04; }
    void setFlagN(bool n) { if (n) F |= 0x02; else F &= ~0x02; }
    void setFlagC(bool c) { if (c) F |= 0x01; else F &= ~0x01; }
    void setFlag53(uint8_t v) { F = static_cast<uint8_t>((F & ~0x28) | (v & 0x28)); }
    void setFlagSZ(uint8_t v) { setFlagS(v); setFlagZ(v); }
    void setFlagSZP(uint8_t v) { setFlagS(v); setFlagZ(v); setFlagPV(parity(v)); }

    static bool parity(uint8_t v);

    /**
     * @brief Fast opcode step (no CPU trace). Selected at startup unless --trace-cpu.
     * @return Instruction T-states (not a running total).
     */
    int execute();
    /**
     * @brief Same as execute() but logs registers before the opcode (RE trace).
     * @return Instruction T-states.
     */
    int execute_traced();

private:
    int op_ED();
    int op_CB();
    int op_DD();
    int op_FD();
    int op_DDCB(uint8_t d);
    int op_FDCB(uint8_t d);

    uint8_t add8(uint8_t a, uint8_t b, bool carry);
    uint8_t sub8(uint8_t a, uint8_t b, bool carry);
    uint8_t inc8(uint8_t a);
    uint8_t dec8(uint8_t a);
    void and8(uint8_t v);
    void xor8(uint8_t v);
    void or8(uint8_t v);
    void cp8(uint8_t v);
    uint16_t add16(uint16_t a, uint16_t b);
    uint16_t adc16(uint16_t a, uint16_t b);
    uint16_t sbc16(uint16_t a, uint16_t b);

    uint8_t rlc(uint8_t v);
    uint8_t rrc(uint8_t v);
    uint8_t rl(uint8_t v);
    uint8_t rr(uint8_t v);
    uint8_t sla(uint8_t v);
    uint8_t sra(uint8_t v);
    uint8_t sll(uint8_t v);
    uint8_t srl(uint8_t v);
    void bit(uint8_t n, uint8_t v);
};
