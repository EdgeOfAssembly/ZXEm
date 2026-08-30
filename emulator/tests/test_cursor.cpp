#include "cursor.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

TEST_CASE("ByteCursor little-endian reads stay in bounds")
{
    const uint8_t raw[] = {0x02, 0x00, 0xFF, 0x11, 0x22};
    ByteCursor c(raw, sizeof(raw));
    uint16_t len = 0;
    REQUIRE(c.get16le(len));
    REQUIRE(len == 2);
    uint8_t flag = 0;
    REQUIRE(c.get8(flag));
    REQUIRE(flag == 0xFF);
    uint8_t b = 0;
    REQUIRE(c.get8(b));
    REQUIRE(b == 0x11);
    REQUIRE(c.get8(b));
    REQUIRE(b == 0x22);
    REQUIRE_FALSE(c.get8(b));
}

TEST_CASE("ByteCursor skip refuses overrun")
{
    const uint8_t raw[] = {1, 2, 3};
    ByteCursor c(raw, sizeof(raw));
    REQUIRE_FALSE(c.skip(4));
    REQUIRE(c.skip(3));
    REQUIRE(c.eof());
}
