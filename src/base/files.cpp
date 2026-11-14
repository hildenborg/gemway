#include "base/bytes.h"
#include <cstdio>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>

namespace base {

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data)
{
    std::string tmp = path + ".tmp~";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp.c_str()); return false; }
    return rename(tmp.c_str(), path.c_str()) == 0;
}

bool writeText(const std::string& path, const std::string& text)
{
    return writeFile(path, std::vector<uint8_t>(text.begin(), text.end()));
}

bool fileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string pathDir(const std::string& path)
{
    size_t p = path.find_last_of('/');
    if (p == std::string::npos) return ".";
    if (p == 0) return "/";
    return path.substr(0, p);
}

std::string pathBase(const std::string& path)
{
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string pathExt(const std::string& path)
{
    std::string b = pathBase(path);
    size_t p = b.find_last_of('.');
    return p == std::string::npos ? std::string() : b.substr(p);
}

std::string pathStem(const std::string& path)
{
    std::string e = pathExt(path);
    return path.substr(0, path.size() - e.size());
}

std::string toUpper(std::string s) { for (auto& c : s) c = char(toupper((unsigned char)c)); return s; }
std::string toLower(std::string s) { for (auto& c : s) c = char(tolower((unsigned char)c)); return s; }

std::string findSibling(const std::string& path, const char* ext)
{
    std::string stem = pathStem(path);
    std::string e = ext;
    for (const std::string& cand : { stem + "." + toLower(e), stem + "." + toUpper(e) })
        if (fileExists(cand)) return cand;
    return {};
}

// ---------------------------------------------------------------- charset
// Atari ST character set, code points 0x80..0xFF.
static const uint16_t kAtariHigh[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x00DF,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x00E3,0x00F5,0x00D8,0x00F8,0x0153,0x0152,0x00C0,0x00C3,0x00D5,0x00A8,0x00B4,0x2020,0x00B6,0x00A9,0x00AE,0x2122,
    0x0133,0x0132,0x05D0,0x05D1,0x05D2,0x05D3,0x05D4,0x05D5,0x05D6,0x05D7,0x05D8,0x05D9,0x05DB,0x05DC,0x05DE,0x05E0,
    0x05E1,0x05E2,0x05E4,0x05E6,0x05E7,0x05E8,0x05E9,0x05EA,0x05DF,0x05DA,0x05DD,0x05E3,0x05E5,0x00A7,0x2227,0x221E,
    0x03B1,0x03B2,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x222E,0x03C6,0x2208,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2022,0x00B7,0x221A,0x207F,0x00B2,0x00B3,0x00AF,
};

uint32_t atariToUnicode(uint8_t c)
{
    if (c < 0x80) return c;
    return kAtariHigh[c - 0x80];
}

int unicodeToAtari(uint32_t cp)
{
    if (cp < 0x80) return int(cp);
    for (int i = 0; i < 128; i++)
        if (kAtariHigh[i] == cp) return 0x80 + i;
    return -1;
}

static void putUtf8(std::string& o, uint32_t cp)
{
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
}

std::string atariToUtf8(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) putUtf8(o, atariToUnicode(c));
    return o;
}

std::string utf8ToAtari(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        i += n;
        int a = unicodeToAtari(cp);
        o += char(a < 0 ? '?' : a);
    }
    return o;
}

} // namespace base
