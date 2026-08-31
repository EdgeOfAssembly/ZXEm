/**
 * @file vfs.h
 * @brief In-place file and zip archive access (no extract-to-disk).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** @brief Largest blob @c vfs_read will load (Spectrum dumps never need more). */
constexpr uint64_t kMaxVfsBytes = 32ull << 20;

/** @brief Bytes loaded from a filesystem path or a zip member. */
struct VfsBlob
{
    std::vector<uint8_t> data;
    std::string path;     /**< Original request (may include #member). */
    std::string member;   /**< Zip member name, empty if a plain file. */
    std::string name;     /**< Basename used for format detection. */
};

/**
 * @brief True if @p n bytes may be loaded into a VfsBlob.
 *
 * @param[in] n Claimed file or zip-member size (uncompressed).
 * @retval true  @p n is 0..kMaxVfsBytes
 * @retval false zip-bomb / hostile size claim
 */
bool vfs_size_ok(uint64_t n);

/**
 * @brief Split `archive.zip#member` / `archive.zip:member` / plain path.
 *
 * @param[in]  spec    User path.
 * @param[out] archive Zip path or the plain file path.
 * @param[out] member  Member inside the zip (empty if none).
 * @return true if @p spec names a zip archive (extension or explicit member).
 */
bool vfs_split_spec(const std::string& spec, std::string& archive, std::string& member);

/** @brief True if @p path exists as a regular file. */
bool vfs_is_file(const std::string& path);

/** @brief True if @p path is a directory. */
bool vfs_is_dir(const std::string& path);

/** @brief True if @p path is a zip (by extension or PK magic). */
bool vfs_is_zip(const std::string& path);

/**
 * @brief Read a file or zip member into memory.
 *
 * Zip members are inflated in RAM via libzip; nothing is written next to the archive.
 * Members and raw files larger than @c kMaxVfsBytes are rejected (logged).
 *
 * @retval true  @p out.data filled
 * @retval false missing file, missing member, oversize, or I/O error (logged)
 */
bool vfs_read(const std::string& spec, VfsBlob& out);

/**
 * @brief List zip members or directory children (non-recursive).
 *
 * @param spec Archive, directory, or `zip#prefix`.
 * @return Names (zip: full member path; dir: file names).
 */
std::vector<std::string> vfs_list(const std::string& spec);

/**
 * @brief Playable ZX extensions (lower-case, with dot).
 */
const std::vector<std::string>& vfs_playable_extensions();

/** @brief True if @p name (basename or member) looks like a ZX image we load. */
bool vfs_is_playable_name(const std::string& name);

/**
 * @brief Filter @p names to playable ZX images, stable-sorted.
 */
std::vector<std::string> vfs_playable(const std::vector<std::string>& names);

/**
 * @brief Find zip members whose path contains @p query (case-insensitive).
 *
 * If @p query is empty, returns all playable members.
 */
std::vector<std::string> vfs_find_members(const std::string& archive, const std::string& query);

/**
 * @brief Expand a directory to playable files (non-recursive, sorted).
 */
std::vector<std::string> vfs_expand_dir(const std::string& dir);

/** @brief Lower-case ASCII copy of @p s. */
std::string vfs_lower(const std::string& s);

/** @brief Basename of a path or zip member (last / or \\). */
std::string vfs_basename(const std::string& path);
