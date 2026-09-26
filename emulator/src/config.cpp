/**
 * @file config.cpp
 * @brief INI loader: quote-aware comments and strtol integers.
 */

#include "config.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>

std::string Config::normalize(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        r.push_back(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/**
 * @brief Truncate @p line at the first `;` or `#` that is not inside quotes.
 *
 * Single- and double-quoted spans are honoured so paths like
 * `"Foo #1/bar.z80"` keep the hash. Quotes themselves are left
 * in place for the caller to strip after trim.
 */
static void strip_ini_comment(char* line)
{
    bool in_single = false;
    bool in_double = false;
    for (char* c = line; *c != '\0'; ++c)
    {
        if (*c == '"' && !in_single)
        {
            in_double = !in_double;
        }
        else if (*c == '\'' && !in_double)
        {
            in_single = !in_single;
        }
        else if (!in_single && !in_double && (*c == ';' || *c == '#'))
        {
            *c = '\0';
            break;
        }
    }
}

bool Config::load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[1024];
    std::string section = "global";
    while (fgets(line, sizeof(line), f)) {
        strip_ini_comment(line);

        // Trim whitespace from both ends
        std::string s = trim(line);

        if (s.empty()) continue;

        if (s.front() == '[' && s.back() == ']') {
            section = normalize(s.substr(1, s.size() - 2));
            continue;
        }

        auto eq = s.find('=');
        if (eq == std::string::npos) continue;

        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);

        key = trim(key);
        val = trim(val);

        // Strip surrounding quotes if present
        if (val.size() >= 2 && ((val.front() == '\"' && val.back() == '\"') ||
                                 (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }

        if (!key.empty()) {
            data_[section][normalize(key)] = val;
        }
    }

    fclose(f);
    return true;
}

bool Config::has(const char* section, const char* key) const {
    auto sit = data_.find(normalize(section));
    if (sit == data_.end()) return false;
    return sit->second.find(normalize(key)) != sit->second.end();
}

std::string Config::getString(const char* section, const char* key, const std::string& defaultValue) const {
    auto sit = data_.find(normalize(section));
    if (sit == data_.end()) return defaultValue;
    auto kit = sit->second.find(normalize(key));
    if (kit == sit->second.end()) return defaultValue;
    return kit->second;
}

int Config::getInt(const char* section, const char* key, int defaultValue) const {
    auto sit = data_.find(normalize(section));
    if (sit == data_.end()) return defaultValue;
    auto kit = sit->second.find(normalize(key));
    if (kit == sit->second.end()) return defaultValue;

    const char* const start = kit->second.c_str();
    const char* p = start;
    while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)))
    {
        ++p;
    }
    if (*p != '+' && *p != '-' && !std::isdigit(static_cast<unsigned char>(*p)))
    {
        return defaultValue;
    }

    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(start, &end, 10);
    if (end == start || errno == ERANGE)
    {
        return defaultValue;
    }
    if (v > static_cast<long>(std::numeric_limits<int>::max()) ||
        v < static_cast<long>(std::numeric_limits<int>::min()))
    {
        return defaultValue;
    }
    return static_cast<int>(v);
}

bool Config::getBool(const char* section, const char* key, bool defaultValue) const {
    auto sit = data_.find(normalize(section));
    if (sit == data_.end()) return defaultValue;
    auto kit = sit->second.find(normalize(key));
    if (kit == sit->second.end()) return defaultValue;
    std::string v = normalize(kit->second);
    return (v == "1" || v == "true" || v == "yes" || v == "on");
}
