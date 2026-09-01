/**
 * @file disk.h
 * @brief EDSK sector map, +3 uPD765 subset, Beta VG93, and disk-image loaders.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <vector>

class Z80;
class ULA;

/** @brief CHS sector identity for an EDSK/+3 image. */
struct SectorId
{
    uint8_t c = 0;
    uint8_t h = 0;
    uint8_t r = 0;

    bool operator<(const SectorId& o) const
    {
        if (c != o.c)
        {
            return c < o.c;
        }
        if (h != o.h)
        {
            return h < o.h;
        }
        return r < o.r;
    }
};

/**
 * @brief One CHS sector with extra copies for EDSK weak/flaky data.
 *
 * @c rr is advanced by @ref DiskMap::find (mutable so find can stay const).
 */
struct SectorSlot
{
    std::vector<std::vector<uint8_t>> copies;
    mutable size_t rr = 0;
    /** @brief Byte offset of copy 0 in @ref DiskMap::backing, or SIZE_MAX. */
    size_t file_off = static_cast<size_t>(-1);
};

/** @brief In-memory CHS sector store (EDSK). */
class DiskMap
{
public:
    std::map<SectorId, SectorSlot> sectors;
    /** @brief Original EDSK bytes; FDC writes patch copy 0 in place when @c file_off is set. */
    std::vector<uint8_t> backing;
    bool dirty = false;

    /**
     * @brief Store a sector copy; appends if CHS already exists (weak sector).
     * @param[in] c Cylinder.
     * @param[in] h Head.
     * @param[in] r Sector ID.
     * @param[in] p Payload bytes.
     * @param[in] n Payload length.
     * @param[in] file_off Offset of this copy in @ref backing, or SIZE_MAX.
     */
    void put(uint8_t c, uint8_t h, uint8_t r, const uint8_t* p, size_t n,
             size_t file_off = static_cast<size_t>(-1));

    /**
     * @brief Replace all copies at CHS with one payload (FDC write).
     * @param[in] c Cylinder.
     * @param[in] h Head.
     * @param[in] r Sector ID.
     * @param[in] p Payload bytes.
     * @param[in] n Payload length.
     */
    void put_replace(uint8_t c, uint8_t h, uint8_t r, const uint8_t* p, size_t n);

    /**
     * @brief Return one copy at CHS, rotating among weak copies on each call.
     * @param[in] c Cylinder.
     * @param[in] h Head.
     * @param[in] r Sector ID.
     * @param[out] len Byte length of the returned copy, or 0 if missing.
     * @return Pointer into the stored copy, or nullptr.
     */
    const uint8_t* find(uint8_t c, uint8_t h, uint8_t r, size_t* len) const;
    bool empty() const { return sectors.empty(); }
};

/**
 * @brief Minimal uPD765 for +3DOS READ/WRITE DATA, SEEK, and RECALIBRATE.
 *
 * Ports: 0x2FFD MSR, 0x3FFD data. Not a full 765.
 */
class Upd765
{
public:
    DiskMap* disk = nullptr;

    Upd765();
    void reset();
    uint8_t read_msr() const;
    uint8_t read_data();
    void write_data(uint8_t val);

private:
    enum Phase
    {
        PhaseCmd,
        PhaseExec,
        PhaseRes
    };

    void start_command();
    int command_length(uint8_t cmd) const;
    void result_st(uint8_t st0, uint8_t st1, uint8_t st2, uint8_t c, uint8_t h, uint8_t r, uint8_t n);

    Phase phase_;
    uint8_t cmd_[9];
    int cmd_need_;
    int cmd_got_;
    uint8_t result_[7];
    int res_len_;
    int res_pos_;
    std::vector<uint8_t> exec_;
    size_t exec_pos_;
    bool exec_write_;
    uint8_t cyl_;
    uint8_t st0_;
    bool interrupt_;
};

/**
 * @brief KR1818VG93 / WD1793 subset used by TR-DOS.
 *
 * Ports (only while TR-DOS ROM is paged): 0x1F/0x3F/0x5F/0x7F/0xFF.
 */
class Vg93
{
public:
    std::vector<uint8_t> image;
    bool dirty = false;
    uint8_t track = 0;
    uint8_t sector = 1;
    uint8_t side = 0;
    uint8_t drive = 0;

    Vg93();
    void reset();
    void set_image(const uint8_t* data, size_t size);
    uint8_t read_status() const;
    uint8_t read_track() const { return track; }
    uint8_t read_sector() const { return sector; }
    uint8_t read_data();
    uint8_t read_system() const;
    void write_command(uint8_t val);
    void write_track(uint8_t val) { track = val; }
    void write_sector(uint8_t val) { sector = val; }
    void write_data(uint8_t val);
    void write_system(uint8_t val);

private:
    uint8_t status_;
    uint8_t data_;
    uint8_t sys_;
    std::vector<uint8_t> buf_;
    size_t buf_pos_;
    size_t write_off_;
    bool drq_;
    bool intrq_;
    bool writing_;
};

bool disk_load_dsk(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool disk_load_mgt(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool disk_load_mdr(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool disk_load_fdi(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool disk_load_udi(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool disk_load_d80(const uint8_t* data, size_t size, Z80& z80, ULA& ula);
bool disk_load_spg(const uint8_t* data, size_t size, Z80& z80, ULA& ula);

/** @brief Attach a raw TRD blob to the VG93 so a user TR-DOS ROM can read it. */
void disk_attach_trd(ULA& ula, const uint8_t* data, size_t size);

/**
 * @brief True if VG93 TRD or EDSK backing was written this session.
 */
bool disk_dirty(const ULA& ula);

/**
 * @brief Write the dirty in-memory disk to @p path (TRD bytes or patched EDSK).
 * @return false if nothing to write or the file could not be created.
 */
bool disk_save(const ULA& ula, const char* path);
