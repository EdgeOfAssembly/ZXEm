/**
 * @file tap_block.c
 * @brief Bounded CBMC model of TAP length-prefixed blocks (tape.cpp load_tap).
 *
 * Walks a nondeterministic buffer of size @c n <= 8, matching ByteCursor
 * (pointer + length + index) and TapeDeck::load_tap:
 * while remaining >= 2, read uint16le @c len; stop if @c len < 2 or the
 * bytes after the length word are fewer than @c len; otherwise consume
 * flag + payload + checksum (@c len bytes, payload @c len-2).
 *
 * Bound @c n <= 8 keeps @c --unwind 8 --unwinding-assertions viable
 * (each accepted block consumes at least 4 bytes; a rejected length word
 * stops the walk).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __CPROVER__
#else
#define __CPROVER_assume(x) ((void)0)
#define __CPROVER_assert(x, m) ((void)0)
#endif

/** Maximum TAP blob modelled; matches Makefile --unwind 8. */
enum
{
    TAP_MAX_N = 8
};

/**
 * @brief Tiny ByteCursor: pointer, logical length, index.
 */
typedef struct
{
    const uint8_t *p;
    size_t n;
    size_t i;
} cursor_t;

/**
 * @brief Bytes not yet consumed.
 *
 * @param[in] c Cursor (must not be NULL).
 * @return @c n - i, or 0 if @c i >= n.
 */
static size_t cursor_remaining(const cursor_t *c)
{
    return (c->i < c->n) ? (c->n - c->i) : 0u;
}

/**
 * @brief Consume one byte if it is inside the logical blob.
 *
 * @param[in,out] c   Cursor.
 * @param[out]    out Destination byte.
 * @retval true  Stored @p out and advanced @c i.
 * @retval false Remaining was 0; cursor unchanged.
 */
static bool cursor_get8(cursor_t *c, uint8_t *out)
{
    if (cursor_remaining(c) < 1u)
    {
        return false;
    }
    __CPROVER_assert(c->i < c->n, "get8: index inside logical length");
    __CPROVER_assert(c->i < (size_t)TAP_MAX_N, "get8: index inside allocation");
    *out = c->p[c->i];
    c->i += 1u;
    return true;
}

/**
 * @brief Consume a little-endian uint16 (two get8s), as ByteCursor::get16le.
 *
 * @param[in,out] c   Cursor.
 * @param[out]    out Destination halfword.
 * @retval true  Both bytes were in range.
 * @retval false Truncated; at most one byte consumed (fail-closed like get8).
 */
static bool cursor_get16le(cursor_t *c, uint16_t *out)
{
    uint8_t lo = 0;
    uint8_t hi = 0;
    if (!cursor_get8(c, &lo) || !cursor_get8(c, &hi))
    {
        return false;
    }
    *out = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return true;
}

/**
 * @brief Advance @p k bytes if they fit in remaining.
 *
 * @param[in,out] c Cursor.
 * @param[in]     k Count to skip (0 is a no-op success).
 * @retval true  Advanced.
 * @retval false Remaining < @p k; cursor unchanged.
 */
static bool cursor_skip(cursor_t *c, size_t k)
{
    if (cursor_remaining(c) < k)
    {
        return false;
    }
    c->i += k;
    __CPROVER_assert(c->i <= c->n, "skip: cursor still <= n");
    return true;
}

/**
 * @brief TAP walk: accept well-formed blocks; never read past @c n.
 *
 * @return 0 (CBMC harness).
 */
int main(void)
{
    uint8_t buf[TAP_MAX_N];
    size_t n;
    __CPROVER_assume(n <= (size_t)TAP_MAX_N);

    cursor_t c;
    c.p = buf;
    c.n = n;
    c.i = 0;

    /*
     * Static fuel equals TAP_MAX_N so --unwind 8 covers the for-loop
     * even on the worst remaining>=2 path; the real parser also stops
     * when a length word is truncated or len is unusable.
     */
    for (uint32_t step = 0; step < (uint32_t)TAP_MAX_N; step++)
    {
        __CPROVER_assert(c.i <= n, "loop: cursor <= n");
        if (cursor_remaining(&c) < 2u)
        {
            break;
        }

        const size_t i_before = c.i;
        uint16_t len = 0;
        if (!cursor_get16le(&c, &len) || len < 2u || cursor_remaining(&c) < (size_t)len)
        {
            __CPROVER_assert(c.i <= n, "stop: cursor <= n");
            break;
        }

        /* flag + payload + checksum: len bytes after the length word. */
        uint8_t flag = 0;
        if (!cursor_get8(&c, &flag))
        {
            break;
        }

        const uint32_t payload = (uint32_t)len - 2u;
        if (!cursor_skip(&c, (size_t)payload))
        {
            break;
        }

        uint8_t checksum = 0;
        if (!cursor_get8(&c, &checksum))
        {
            break;
        }

        (void)flag;
        (void)checksum;

        __CPROVER_assert(c.i <= n, "accepted: cursor <= n");
        __CPROVER_assert(payload + 2u == (uint32_t)len,
                         "accepted: payload+2 == len (uint32, no wrap)");
        __CPROVER_assert(c.i == i_before + 2u + (size_t)len,
                         "accepted: consumed length word plus len data bytes");
        __CPROVER_assert(c.i <= (size_t)TAP_MAX_N, "accepted: never past allocation");
    }

    __CPROVER_assert(c.i <= n, "final: never read past n");
    return 0;
}
