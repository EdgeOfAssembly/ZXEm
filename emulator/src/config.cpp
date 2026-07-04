#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <algorithm>

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

bool Config::load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[1024];
    std::string section = "global";
    while (fgets(line, sizeof(line), f)) {
        // Strip comments
        char* c = line;
        while (*c) {
            if (*c == ';' || *c == '#') {
                *c = '\0';
                break;
            }
            c++;
        }

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
    return std::atoi(kit->second.c_str());
}

bool Config::getBool(const char* section, const char* key, bool defaultValue) const {
    auto sit = data_.find(normalize(section));
    if (sit == data_.end()) return defaultValue;
    auto kit = sit->second.find(normalize(key));
    if (kit == sit->second.end()) return defaultValue;
    std::string v = normalize(kit->second);
    return (v == "1" || v == "true" || v == "yes" || v == "on");
}
