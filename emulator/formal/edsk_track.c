/**
 * @file edsk_track.c
 * @brief CBMC harness: EDSK per-track size table cannot overflow 32-bit bytes.
 */

#include <stdint.h>

#ifdef __CPROVER__
#else
#define __CPROVER_assume(x) ((void)0)
#define __CPROVER_assert(x, m) ((void)0)
#endif

/**
 * @brief Convert an EDSK track-size table byte to a byte count.
 *
 * @param[in] size_byte Table entry (track length / 256).
 * @return Track length in bytes.
 */
uint32_t edsk_track_bytes(uint8_t size_byte)
{
    return (uint32_t)size_byte * 256u;
}

int main(void)
{
    uint8_t size_byte;
    const uint32_t n = edsk_track_bytes(size_byte);
    __CPROVER_assert(n == (uint32_t)size_byte * 256u, "scale");
    __CPROVER_assert(n <= 255u * 256u, "bound");
    __CPROVER_assert((n % 256u) == 0u, "aligned");
    return 0;
}
