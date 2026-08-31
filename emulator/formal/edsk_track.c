/**
 * @file edsk_track.c
 * @brief Bounded CBMC model of EDSK extended track-size cursor walk (disk.cpp).
 *
 * parse_edsk starts at pos = 0x100 and for each track-size table byte
 * computes tsz = size_byte * 256. Zero-size tracks are skipped; if
 * pos + tsz would pass the file length, the walk stops (no add).
 * ntracks <= 3 so --unwind 4 --unwinding-assertions stays viable
 * (a 4-trip loop fails the unwinding assertion at unwind 4; CBMC
 * needs one spare unrolling to prove exit).
 */

#include <stdint.h>

#ifdef __CPROVER__
#else
#define __CPROVER_assume(x) ((void)0)
#define __CPROVER_assert(x, m) ((void)0)
#endif

/**
 * Maximum tracks modelled under Makefile --unwind 4.
 * A 4-iteration loop needs --unwind 5 with --unwinding-assertions.
 */
enum
{
    EDSK_MAX_TRACKS = 3,
    EDSK_HEADER = 0x100,
    EDSK_MAX_FILE = 1024
};

/**
 * @brief Convert an EDSK track-size table byte to a byte count.
 *
 * @param[in] size_byte Table entry (track length / 256).
 * @return Track length in bytes (@p size_byte * 256).
 */
uint32_t edsk_track_bytes(uint8_t size_byte)
{
    return (uint32_t)size_byte * 256u;
}

/**
 * @brief Walk a nondeterministic EDSK track-size table without wrapping.
 *
 * @return 0 (CBMC harness).
 */
int main(void)
{
    uint32_t n;
    __CPROVER_assume(n >= (uint32_t)EDSK_HEADER);
    __CPROVER_assume(n <= (uint32_t)EDSK_MAX_FILE);

    uint8_t ntracks;
    __CPROVER_assume(ntracks <= (uint8_t)EDSK_MAX_TRACKS);

    uint32_t pos = (uint32_t)EDSK_HEADER;

    for (uint32_t i = 0; i < (uint32_t)ntracks; i++)
    {
        __CPROVER_assert(i < (uint32_t)EDSK_MAX_TRACKS, "track index in table");

        uint8_t size_byte;
        const uint32_t tsz = edsk_track_bytes(size_byte);

        __CPROVER_assert(tsz == (uint32_t)size_byte * 256u, "scale");
        __CPROVER_assert(tsz <= 255u * 256u, "per-track bound");
        __CPROVER_assert((tsz % 256u) == 0u, "aligned");

        if (tsz == 0u)
        {
            continue;
        }

        /* Matching parse_edsk: stop without adding if the track would pass EOF. */
        if (pos + tsz > n)
        {
            break;
        }

        __CPROVER_assert(pos <= UINT32_MAX - tsz, "pos+tsz does not wrap uint32");
        __CPROVER_assert(pos + tsz >= pos, "pos+tsz does not wrap size_t/uint32");
        __CPROVER_assert(pos + tsz <= n, "track fits in file");

        pos += tsz;
    }

    __CPROVER_assert(pos <= n, "cursor stays inside file");
    __CPROVER_assert(pos >= (uint32_t)EDSK_HEADER, "cursor never before header");
    return 0;
}
