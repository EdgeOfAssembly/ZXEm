/**
 * @file edsk_track.c
 * @brief Bounded CBMC model of EDSK extended track-size accumulation (disk.cpp).
 *
 * parse_edsk uses @c track_bytes = size_byte * 256u for each extended-disk
 * track-size table entry. This harness sums a nondeterministic number of
 * tracks with @c ntracks <= 3 so @c --unwind 4 --unwinding-assertions stays
 * viable (a 4-trip loop fails the unwinding assertion at unwind 4; CBMC
 * needs one spare unrolling to prove exit). Proves the add cannot wrap
 * uint32 and the total matches the algebraic sum, with the closed bound
 * 4 * 255 * 256.
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
    EDSK_MAX_TRACKS = 3
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
 * @brief Accumulate @c ntracks table entries without uint32 overflow.
 *
 * @return 0 (CBMC harness).
 */
int main(void)
{
    uint8_t ntracks;
    __CPROVER_assume(ntracks <= (uint8_t)EDSK_MAX_TRACKS);

    uint8_t size_byte[EDSK_MAX_TRACKS];
    uint32_t total = 0;
    uint32_t raw_sum = 0;

    for (uint32_t i = 0; i < (uint32_t)ntracks; i++)
    {
        __CPROVER_assert(i < (uint32_t)EDSK_MAX_TRACKS, "track index in table");
        const uint8_t sb = size_byte[i];
        const uint32_t tsz = edsk_track_bytes(sb);

        __CPROVER_assert(tsz == (uint32_t)sb * 256u, "scale");
        __CPROVER_assert(tsz <= 255u * 256u, "per-track bound");
        __CPROVER_assert((tsz % 256u) == 0u, "aligned");
        __CPROVER_assert(total <= UINT32_MAX - tsz, "no overflow");

        total += tsz;
        raw_sum += (uint32_t)sb;
    }

    __CPROVER_assert(total == raw_sum * 256u, "total == sum of size_byte * 256");
    __CPROVER_assert(total <= 4u * 255u * 256u, "total <= 4 * 255 * 256");
    return 0;
}
