#include "config.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace
{

bool write_ini(const std::string& path, const char* body)
{
    FILE* f = fopen(path.c_str(), "w");
    if (f == nullptr)
    {
        return false;
    }
    const int n = std::fputs(body, f);
    fclose(f);
    return n >= 0;
}

} // namespace

TEST_CASE("Config keeps # and ; inside quoted values")
{
    char dir[] = "/tmp/zxem-cfg-XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string path = std::string(dir) + "/c.ini";
    REQUIRE(write_ini(path,
                      "game = \"Foo #1/bar.z80\"\n"
                      "note = 'a;b#c'\n"));

    Config cfg;
    REQUIRE(cfg.load(path.c_str()));
    REQUIRE(cfg.getString("global", "game") == "Foo #1/bar.z80");
    REQUIRE(cfg.getString("global", "note") == "a;b#c");

    unlink(path.c_str());
    rmdir(dir);
}

TEST_CASE("Config strips unquoted ; comments")
{
    char dir[] = "/tmp/zxem-cfg-XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string path = std::string(dir) + "/c.ini";
    REQUIRE(write_ini(path, "game = a.z80  ; comment\n"));

    Config cfg;
    REQUIRE(cfg.load(path.c_str()));
    REQUIRE(cfg.getString("global", "game") == "a.z80");

    unlink(path.c_str());
    rmdir(dir);
}

TEST_CASE("Config::getInt uses strtol and rejects non-numeric")
{
    char dir[] = "/tmp/zxem-cfg-XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string path = std::string(dir) + "/c.ini";
    REQUIRE(write_ini(path,
                      "bad = xyz\n"
                      "ok = 4\n"
                      "junk = 3foo\n"));

    Config cfg;
    REQUIRE(cfg.load(path.c_str()));
    REQUIRE(cfg.getInt("global", "bad", 7) == 7);
    REQUIRE(cfg.getInt("global", "ok", 0) == 4);
    REQUIRE(cfg.getInt("global", "junk", 0) == 3);
    REQUIRE(cfg.getInt("global", "missing", 9) == 9);

    unlink(path.c_str());
    rmdir(dir);
}

TEST_CASE("Config reads hardware.issue")
{
    char dir[] = "/tmp/zxem-cfg-XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string path = std::string(dir) + "/c.ini";
    REQUIRE(write_ini(path, "[hardware]\nissue = 2\n"));

    Config cfg;
    REQUIRE(cfg.load(path.c_str()));
    REQUIRE(cfg.getString("hardware", "issue") == "2");

    unlink(path.c_str());
    rmdir(dir);
}
