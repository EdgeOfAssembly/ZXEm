#pragma once
#include <string>
#include <map>
#include <string>

// Minimal INI-style config loader.
// Sections are optional; keys are case-insensitive.
// Format:
//   [section]
//   key = value
//   ; comment
//   # comment

class Config {
public:
    bool load(const char* path);
    bool has(const char* section, const char* key) const;
    std::string getString(const char* section, const char* key, const std::string& defaultValue = "") const;
    int getInt(const char* section, const char* key, int defaultValue = 0) const;
    bool getBool(const char* section, const char* key, bool defaultValue = false) const;

private:
    std::map<std::string, std::map<std::string, std::string>> data_;
    static std::string normalize(const std::string& s);
};
