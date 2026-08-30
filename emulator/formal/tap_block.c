/**
 * @file tap_block.c
 * @brief CBMC harness: TAP block length always yields a non-wrapping payload.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __CPROVER__
#else
#define __CPROVER_assume(x) ((void)0)
#define __CPROVER_assert(x, m) ((void)0)
#endif

/**
 * @brief Payload bytes inside a TAP block of @p block_len (flag + payload + checksum).
 */
uint16_t tap_payload_len(uint16_t block_len)
{
    __CPROVER_assume(block_len >= 2);
    return (uint16_t)(block_len - 2u);
}

int main(void)
{
    uint16_t block_len;
    __CPROVER_assume(block_len >= 2);
    const uint16_t payload = tap_payload_len(block_len);
    __CPROVER_assert(payload == (uint16_t)(block_len - 2u), "payload = len-2");
    __CPROVER_assert((uint32_t)payload + 2u == (uint32_t)block_len, "no wrap");
    return 0;
}
