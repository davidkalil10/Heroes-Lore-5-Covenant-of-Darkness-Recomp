#include "trans.h"
#include "guest.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace Trans {

static std::unordered_map<std::string, std::string> s_dict;
static std::unordered_map<std::string, u32> s_guestCache;
static bool s_inited = false;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void init(const std::string& customPath) {
    if (s_inited) return;
    s_inited = true;

    std::vector<std::string> candidates;
    if (!customPath.empty()) candidates.push_back(customPath);
    candidates.push_back("data/dict.bin");
    candidates.push_back("dist/dict.bin");
    candidates.push_back("../data/dict.bin");
    candidates.push_back("dict.bin");

    std::string foundPath;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) {
        fprintf(stderr, "[TRANS] Warning: dict.bin not found in search paths!\n");
        return;
    }

    FILE* f = fopen(foundPath.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[TRANS] Error: cannot open %s\n", foundPath.c_str());
        return;
    }

    u32 count = 0;
    if (fread(&count, 4, 1, f) != 1) {
        fclose(f);
        return;
    }

    std::vector<char> kbuf;
    std::vector<char> vbuf;
    for (u32 i = 0; i < count; i++) {
        u16 klen = 0, vlen = 0;
        if (fread(&klen, 2, 1, f) != 1) break;
        if (fread(&vlen, 2, 1, f) != 1) break;
        if (kbuf.size() < (size_t)klen + 1) kbuf.resize(klen + 1);
        if (vbuf.size() < (size_t)vlen + 1) vbuf.resize(vlen + 1);
        if (fread(kbuf.data(), 1, klen, f) != klen) break;
        if (fread(vbuf.data(), 1, vlen, f) != vlen) break;
        kbuf[klen] = '\0';
        vbuf[vlen] = '\0';
        s_dict[std::string(kbuf.data(), klen)] = std::string(vbuf.data(), vlen);
    }
    fclose(f);
    fprintf(stderr, "[TRANS] Loaded %zu translations from %s\n", s_dict.size(), foundPath.c_str());
}

static bool hasKorean(const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if (*p >= 0x80) return true;
    }
    return false;
}

static std::string translatePiece(const std::string& piece) {
    if (piece.empty()) return "";
    std::string s = trim(piece);
    if (s.empty()) return piece;

    auto it = s_dict.find(s);
    if (it != s_dict.end()) return it->second;

    // Split leading formatting / control tokens like @0, ^, #, etc.
    size_t i = 0;
    while (i < s.size() && (unsigned char)s[i] < 0x80 && !isalnum((unsigned char)s[i])) {
        i++;
    }
    std::string pfx = s.substr(0, i);
    std::string core = s.substr(i);

    it = s_dict.find(core);
    if (it != s_dict.end()) return pfx + it->second;

    // Check trimmed core
    std::string trimmedCore = trim(core);
    it = s_dict.find(trimmedCore);
    if (it != s_dict.end()) return pfx + it->second;

    return piece;
}

static std::string translateCompound(const std::string& text) {
    if (text.find(';') == std::string::npos) {
        return translatePiece(text);
    }

    std::stringstream ss(text);
    std::string segment;
    std::string result;
    bool first = true;
    while (std::getline(ss, segment, ';')) {
        if (!first) result += ";";
        first = false;
        result += translatePiece(segment);
    }
    return result;
}

u32 translatePtr(u32 strPtr, int maxLen) {
    if (strPtr < 0x1000 || strPtr >= gm::MEM_SIZE) return strPtr;

    std::string orig = gm::cstr(strPtr);
    const char* raw = orig.c_str();
    bool korean = hasKorean(raw);

    // If it's already English and within maxLen (or maxLen not set), return as is
    if (!korean && (maxLen <= 0 || (int)orig.size() <= maxLen)) {
        return strPtr;
    }

    std::string key = (maxLen > 0) ? (orig + "@len" + std::to_string(maxLen)) : orig;

    auto itCache = s_guestCache.find(key);
    if (itCache != s_guestCache.end()) return itCache->second;

    if (!s_inited) init();

    std::string trans = korean ? translateCompound(orig) : orig;

    if (maxLen > 3 && (int)trans.size() > maxLen) {
        trans = trans.substr(0, maxLen - 2) + "..";
    }

    u32 bufSz = (u32)trans.size() + 1;
    u32 guestBuf = gm::malloc_(bufSz + 4);
    memcpy(gm::ptr(guestBuf, bufSz), trans.c_str(), bufSz);
    s_guestCache[key] = guestBuf;
    fprintf(stderr, "[TRANS] '%s' -> '%s' (0x%08x -> 0x%08x)\n", orig.c_str(), trans.c_str(), strPtr, guestBuf);

    return guestBuf;
}

} // namespace Trans
