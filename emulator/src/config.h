/**
 * @file config.h
 * @brief Minimal INI-style config loader (case-insensitive keys, quoted values).
 */
#pragma once

#include <map>
#include <string>

/**
 * @brief INI loader: optional `[section]`, `key = value`, `;`/`#` comments.
 *
 * Comments are stripped only outside single or double quotes so a value like
 * `"Foo #1/bar.z80"` keeps the hash. Matching surrounding quotes
 * are removed after trim. Keys and section names are case-insensitive.
 */
class Config {
public:
    bool load(const char* path);
    bool has(const char* section, const char* key) const;
    std::string getString(const char* section, const char* key, const std::string& defaultValue = "") const;

    /**
     * @brief Integer for @p key, or @p defaultValue if missing or not a number.
     *
     * Uses @c std::strtol (base 10). If the first non-space character is not a
     * digit, `+`, or `-`, returns @p defaultValue (so `"xyz"` is not 0).
     * Trailing junk after a leading number is accepted (`"3foo"` → 3).
     * No conversion (`endptr == start`) also returns @p defaultValue.
     */
    int getInt(const char* section, const char* key, int defaultValue = 0) const;
    bool getBool(const char* section, const char* key, bool defaultValue = false) const;

private:
    std::map<std::string, std::map<std::string, std::string>> data_;
    static std::string normalize(const std::string& s);
};
