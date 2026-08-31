/**
 * @file vfs.cpp
 * @brief Filesystem + libzip in-place reader.
 */

#include "vfs.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <zip.h>

namespace
{

/** @brief Refuse to walk a zip central directory larger than this (zip bomb). */
constexpr zip_int64_t kMaxVfsZipEntries = 1 << 20;

/**
 * @brief True if @p z has a sane number of members.
 *
 * @param[in] z       Open archive.
 * @param[in] archive Path used only in the error log.
 * @retval false @c zip_get_num_entries failed or exceeded @c kMaxVfsZipEntries
 */
bool zip_entry_count_ok(zip_t* z, const std::string& archive)
{
    const zip_int64_t n = zip_get_num_entries(z, 0);
    if (n >= 0 && n <= kMaxVfsZipEntries)
    {
        return true;
    }
    log_error("zip entry count insane: %lld (cap %lld) in %s",
              static_cast<long long>(n),
              static_cast<long long>(kMaxVfsZipEntries),
              archive.c_str());
    return false;
}

bool has_zip_magic(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr)
    {
        return false;
    }
    unsigned char mag[4] = {0, 0, 0, 0};
    const size_t n = fread(mag, 1, 4, f);
    fclose(f);
    if (n < 4)
    {
        return false;
    }
    return mag[0] == 'P' && mag[1] == 'K' && (mag[2] == 0x03 || mag[2] == 0x05 || mag[2] == 0x07);
}

std::string ext_of(const std::string& name)
{
    const std::string base = vfs_basename(name);
    const auto dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
    {
        return {};
    }
    return vfs_lower(base.substr(dot));
}

} // namespace

bool vfs_size_ok(uint64_t n)
{
    return n <= kMaxVfsBytes;
}

std::string vfs_lower(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
    {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string vfs_basename(const std::string& path)
{
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
    {
        return path;
    }
    return path.substr(pos + 1);
}

bool vfs_is_file(const std::string& path)
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
    {
        return false;
    }
    return S_ISREG(st.st_mode);
}

bool vfs_is_dir(const std::string& path)
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
    {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool vfs_is_zip(const std::string& path)
{
    const std::string e = ext_of(path);
    if (e == ".zip")
    {
        return vfs_is_file(path);
    }
    return vfs_is_file(path) && has_zip_magic(path);
}

bool vfs_split_spec(const std::string& spec, std::string& archive, std::string& member)
{
    archive.clear();
    member.clear();

    const auto hash = spec.find('#');
    if (hash != std::string::npos)
    {
        archive = spec.substr(0, hash);
        member = spec.substr(hash + 1);
        return true;
    }

    /* archive.zip:Games/foo.tap — only if the prefix exists as a zip file. */
    const auto colon = spec.find(':');
    if (colon != std::string::npos && colon > 1)
    {
        const std::string prefix = spec.substr(0, colon);
        if (vfs_is_zip(prefix))
        {
            archive = prefix;
            member = spec.substr(colon + 1);
            return true;
        }
    }

    archive = spec;
    return vfs_is_zip(spec);
}

const std::vector<std::string>& vfs_playable_extensions()
{
    static const std::vector<std::string> k = {
        ".tap", ".tzx", ".z80", ".sna", ".szx", ".sp", ".slt",
        ".trd", ".scl", ".dsk", ".mgt", ".fdi", ".udi",
        ".mdr", ".dck", ".rom", ".csw", ".d80", ".d40",
        ".ipf", ".spg", ".pok", ".zip"
    };
    return k;
}

bool vfs_is_playable_name(const std::string& name)
{
    if (name.empty() || name.back() == '/')
    {
        return false;
    }
    const std::string e = ext_of(name);
    if (e.empty() || e == ".zip")
    {
        return false;
    }
    const auto& exts = vfs_playable_extensions();
    return std::find(exts.begin(), exts.end(), e) != exts.end();
}

std::vector<std::string> vfs_playable(const std::vector<std::string>& names)
{
    std::vector<std::string> out;
    out.reserve(names.size());
    for (const auto& n : names)
    {
        if (vfs_is_playable_name(n))
        {
            out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

static zip_t* zip_open_read(const std::string& path)
{
    int err = 0;
    zip_t* z = zip_open(path.c_str(), ZIP_RDONLY, &err);
    if (z == nullptr)
    {
        zip_error_t ze;
        zip_error_init_with_code(&ze, err);
        log_error("zip open failed: %s (%s)", path.c_str(), zip_error_strerror(&ze));
        zip_error_fini(&ze);
        return nullptr;
    }
    return z;
}

std::vector<std::string> vfs_list(const std::string& spec)
{
    std::string archive;
    std::string member;
    const bool is_zip = vfs_split_spec(spec, archive, member);

    if (is_zip || vfs_is_zip(archive))
    {
        zip_t* z = zip_open_read(archive);
        if (z == nullptr)
        {
            return {};
        }
        if (!zip_entry_count_ok(z, archive))
        {
            zip_close(z);
            return {};
        }
        std::vector<std::string> names;
        const zip_int64_t n = zip_get_num_entries(z, 0);
        names.reserve(n > 0 ? static_cast<size_t>(n) : 0);
        for (zip_int64_t i = 0; i < n; i++)
        {
            const char* nm = zip_get_name(z, static_cast<zip_uint64_t>(i), 0);
            if (nm == nullptr)
            {
                continue;
            }
            if (!member.empty())
            {
                const std::string hay = vfs_lower(nm);
                const std::string needle = vfs_lower(member);
                if (hay.find(needle) == std::string::npos)
                {
                    continue;
                }
            }
            names.emplace_back(nm);
        }
        zip_close(z);
        return names;
    }

    if (vfs_is_dir(spec))
    {
        return vfs_expand_dir(spec);
    }

    if (vfs_is_file(spec))
    {
        return {spec};
    }
    return {};
}

std::vector<std::string> vfs_find_members(const std::string& archive, const std::string& query)
{
    const auto all = vfs_list(archive);
    if (query.empty())
    {
        return vfs_playable(all);
    }
    const std::string needle = vfs_lower(query);
    std::vector<std::string> out;
    for (const auto& n : all)
    {
        if (!vfs_is_playable_name(n))
        {
            continue;
        }
        if (vfs_lower(n).find(needle) != std::string::npos)
        {
            out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> vfs_expand_dir(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (d == nullptr)
    {
        log_error("cannot open directory: %s", dir.c_str());
        return {};
    }
    std::vector<std::string> out;
    while (true)
    {
        struct dirent* ent = readdir(d);
        if (ent == nullptr)
        {
            break;
        }
        if (ent->d_name[0] == '.')
        {
            continue;
        }
        std::string full = dir;
        if (!full.empty() && full.back() != '/')
        {
            full.push_back('/');
        }
        full += ent->d_name;
        if (!vfs_is_file(full))
        {
            continue;
        }
        if (vfs_is_playable_name(ent->d_name))
        {
            out.push_back(full);
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool vfs_read(const std::string& spec, VfsBlob& out)
{
    out = VfsBlob{};
    out.path = spec;

    std::string archive;
    std::string member;
    const bool zip_spec = vfs_split_spec(spec, archive, member);

    if (zip_spec && !member.empty())
    {
        zip_t* z = zip_open_read(archive);
        if (z == nullptr)
        {
            return false;
        }
        if (!zip_entry_count_ok(z, archive))
        {
            zip_close(z);
            return false;
        }

        zip_int64_t idx = zip_name_locate(z, member.c_str(), 0);
        if (idx < 0)
        {
            /* Case-insensitive / substring locate. Prefer snapshots over tapes. */
            auto hits = vfs_find_members(archive, member);
            if (hits.size() > 1)
            {
                static const char* rank[] = {
                    ".z80", ".sna", ".szx", ".sp", ".slt", ".tap", ".tzx",
                    ".scl", ".trd", ".dsk", ".rom"
                };
                std::vector<std::string> best;
                for (const char* ext : rank)
                {
                    for (const auto& h : hits)
                    {
                        if (vfs_lower(h).size() >= std::strlen(ext) &&
                            vfs_lower(h).compare(vfs_lower(h).size() - std::strlen(ext),
                                                 std::strlen(ext), ext) == 0)
                        {
                            best.push_back(h);
                        }
                    }
                    if (!best.empty())
                    {
                        hits.swap(best);
                        break;
                    }
                }
            }
            if (hits.size() >= 1)
            {
                if (hits.size() > 1)
                {
                    log_info("zip member '%s': %zu matches, using %s",
                             member.c_str(), hits.size(), hits[0].c_str());
                }
                idx = zip_name_locate(z, hits[0].c_str(), 0);
                member = hits[0];
            }
        }
        if (idx < 0)
        {
            log_error("zip member not found: %s in %s", member.c_str(), archive.c_str());
            zip_close(z);
            return false;
        }

        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(z, static_cast<zip_uint64_t>(idx), 0, &st) != 0 ||
            (st.valid & ZIP_STAT_SIZE) == 0)
        {
            log_error("zip stat failed for %s", member.c_str());
            zip_close(z);
            return false;
        }
        if (!vfs_size_ok(st.size))
        {
            log_error("zip member too large: %s (%llu bytes, cap %llu)",
                      member.c_str(),
                      static_cast<unsigned long long>(st.size),
                      static_cast<unsigned long long>(kMaxVfsBytes));
            zip_close(z);
            return false;
        }

        zip_file_t* zf = zip_fopen_index(z, static_cast<zip_uint64_t>(idx), 0);
        if (zf == nullptr)
        {
            log_error("zip fopen failed: %s", member.c_str());
            zip_close(z);
            return false;
        }

        out.data.resize(static_cast<size_t>(st.size));
        zip_int64_t got = 0;
        if (st.size > 0)
        {
            got = zip_fread(zf, out.data.data(), st.size);
        }
        zip_fclose(zf);
        zip_close(z);
        if (got < 0 || static_cast<uint64_t>(got) != st.size)
        {
            log_error("zip read short: %s (got %lld want %llu)",
                      member.c_str(), static_cast<long long>(got),
                      static_cast<unsigned long long>(st.size));
            return false;
        }
        out.member = member;
        out.name = vfs_basename(member);
        log_info("vfs zip member %s (%zu bytes, no extract) from %s",
                 member.c_str(), out.data.size(), archive.c_str());
        return true;
    }

    if (zip_spec && member.empty())
    {
        log_error("zip given without member: %s (use --list or zip#path)", archive.c_str());
        return false;
    }

    FILE* f = fopen(spec.c_str(), "rb");
    if (f == nullptr)
    {
        log_error("cannot open file: %s", spec.c_str());
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    const long sz = ftell(f);
    if (sz < 0)
    {
        fclose(f);
        return false;
    }
    if (!vfs_size_ok(static_cast<uint64_t>(sz)))
    {
        log_error("file too large: %s (%ld bytes, cap %llu)",
                  spec.c_str(), sz,
                  static_cast<unsigned long long>(kMaxVfsBytes));
        fclose(f);
        return false;
    }
    rewind(f);
    out.data.resize(static_cast<size_t>(sz));
    if (sz > 0)
    {
        const size_t n = fread(out.data.data(), 1, static_cast<size_t>(sz), f);
        fclose(f);
        if (n != static_cast<size_t>(sz))
        {
            log_error("short read: %s", spec.c_str());
            return false;
        }
    }
    else
    {
        fclose(f);
    }
    out.name = vfs_basename(spec);
    log_debug("vfs file %s (%zu bytes)", spec.c_str(), out.data.size());
    return true;
}
