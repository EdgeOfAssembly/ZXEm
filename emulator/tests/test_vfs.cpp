#include "vfs.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
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

TEST_CASE("kMaxVfsBytes is 32 MiB")
{
    REQUIRE(kMaxVfsBytes == (32ull << 20));
}

TEST_CASE("vfs_size_ok rejects oversize claims without allocating")
{
    REQUIRE(vfs_size_ok(0));
    REQUIRE(vfs_size_ok(1));
    REQUIRE(vfs_size_ok(kMaxVfsBytes));
    REQUIRE_FALSE(vfs_size_ok(kMaxVfsBytes + 1));
    REQUIRE_FALSE(vfs_size_ok(UINT64_MAX));
}

static std::vector<uint8_t> slurp_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    REQUIRE(fseek(f, 0, SEEK_END) == 0);
    const long n = ftell(f);
    REQUIRE(n >= 0);
    rewind(f);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0)
    {
        REQUIRE(fread(b.data(), 1, static_cast<size_t>(n), f) == static_cast<size_t>(n));
    }
    fclose(f);
    return b;
}

static void dump_file(const std::string& path, const std::vector<uint8_t>& b)
{
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    if (!b.empty())
    {
        REQUIRE(fwrite(b.data(), 1, b.size(), f) == b.size());
    }
    fclose(f);
}

/** Patch local-header and central-directory uncompressed-size fields. */
static void patch_zip_uncomp_size(std::vector<uint8_t>& z, uint32_t new_size)
{
    bool any = false;
    for (size_t i = 0; i + 26 < z.size(); i++)
    {
        if (z[i] == 'P' && z[i + 1] == 'K' && z[i + 2] == 0x03 && z[i + 3] == 0x04)
        {
            z[i + 22] = static_cast<uint8_t>(new_size);
            z[i + 23] = static_cast<uint8_t>(new_size >> 8);
            z[i + 24] = static_cast<uint8_t>(new_size >> 16);
            z[i + 25] = static_cast<uint8_t>(new_size >> 24);
            any = true;
        }
        if (z[i] == 'P' && z[i + 1] == 'K' && z[i + 2] == 0x01 && z[i + 3] == 0x02
            && i + 28 < z.size())
        {
            z[i + 24] = static_cast<uint8_t>(new_size);
            z[i + 25] = static_cast<uint8_t>(new_size >> 8);
            z[i + 26] = static_cast<uint8_t>(new_size >> 16);
            z[i + 27] = static_cast<uint8_t>(new_size >> 24);
            any = true;
        }
    }
    REQUIRE(any);
}

TEST_CASE("vfs_read rejects a zip member whose claimed size exceeds kMaxVfsBytes")
{
    char dir[] = "/tmp/zxem-vfs-XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string zip_path = std::string(dir) + "/bomb.zip";

    int err = 0;
    zip_t* z = zip_open(zip_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(z != nullptr);
    const char payload[] = "ZX";
    zip_source_t* src = zip_source_buffer(z, payload, 2, 0);
    REQUIRE(src != nullptr);
    REQUIRE(zip_file_add(z, "Games/bomb.tap", src, ZIP_FL_OVERWRITE) >= 0);
    REQUIRE(zip_close(z) == 0);

    std::vector<uint8_t> raw = slurp_file(zip_path);
    patch_zip_uncomp_size(raw, static_cast<uint32_t>(kMaxVfsBytes + 1));
    dump_file(zip_path, raw);

    VfsBlob blob;
    REQUIRE_FALSE(vfs_read(zip_path + "#Games/bomb.tap", blob));
    REQUIRE(blob.data.size() < kMaxVfsBytes);

    unlink(zip_path.c_str());
    rmdir(dir);
}

TEST_CASE("vfs_read rejects a sparse file larger than kMaxVfsBytes")
{
    char dir[] = "/tmp/zxem-vfs-XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string path = std::string(dir) + "/huge.bin";
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    REQUIRE(ftruncate(fileno(f), static_cast<off_t>(kMaxVfsBytes) + 1) == 0);
    fclose(f);

    VfsBlob blob;
    REQUIRE_FALSE(vfs_read(path, blob));
    REQUIRE(blob.data.empty());

    unlink(path.c_str());
    rmdir(dir);
}
