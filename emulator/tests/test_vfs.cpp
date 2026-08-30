#include "vfs.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include <zip.h>

TEST_CASE("vfs_split_spec understands hash members")
{
    std::string archive;
    std::string member;
    const bool zip = vfs_split_spec("/mnt/Games.zip#Games/Manic Miner/x.z80", archive, member);
    REQUIRE(zip);
    REQUIRE(archive == "/mnt/Games.zip");
    REQUIRE(member == "Games/Manic Miner/x.z80");
}

TEST_CASE("vfs_is_playable_name accepts ZX extensions")
{
    REQUIRE(vfs_is_playable_name("foo.TAP"));
    REQUIRE(vfs_is_playable_name("a/b/c.scl"));
    REQUIRE_FALSE(vfs_is_playable_name("readme.txt"));
    REQUIRE_FALSE(vfs_is_playable_name("Games/"));
}

TEST_CASE("vfs_read loads a zip member without extracting to disk")
{
    char dir[] = "/tmp/zxem-vfs-XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string zip_path = std::string(dir) + "/t.zip";
    const std::string leaked = std::string(dir) + "/leaked.tap";

    int err = 0;
    zip_t* z = zip_open(zip_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(z != nullptr);
    const char payload[] = "ZX";
    zip_source_t* src = zip_source_buffer(z, payload, 2, 0);
    REQUIRE(src != nullptr);
    REQUIRE(zip_file_add(z, "Games/hi.tap", src, ZIP_FL_OVERWRITE) >= 0);
    REQUIRE(zip_close(z) == 0);

    VfsBlob blob;
    REQUIRE(vfs_read(zip_path + "#Games/hi.tap", blob));
    REQUIRE(blob.data.size() == 2);
    REQUIRE(blob.data[0] == 'Z');
    REQUIRE(blob.member == "Games/hi.tap");
    REQUIRE(access(leaked.c_str(), F_OK) != 0);

    unlink(zip_path.c_str());
    rmdir(dir);
}
