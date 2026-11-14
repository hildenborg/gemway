// Big-endian (Motorola 68000) byte helpers used by all Atari file formats.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace base {

inline uint16_t rd16(const std::vector<uint8_t>& b, size_t o)
{
    if (o + 2 > b.size()) return 0;
    return uint16_t((b[o] << 8) | b[o + 1]);
}
inline int16_t rds16(const std::vector<uint8_t>& b, size_t o) { return int16_t(rd16(b, o)); }
inline uint32_t rd32(const std::vector<uint8_t>& b, size_t o)
{
    return (uint32_t(rd16(b, o)) << 16) | rd16(b, o + 2);
}

class Writer {
public:
    std::vector<uint8_t> d;
    size_t pos() const { return d.size(); }
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { d.push_back(uint8_t(v >> 8)); d.push_back(uint8_t(v)); }
    void u32(uint32_t v) { u16(uint16_t(v >> 16)); u16(uint16_t(v)); }
    void bytes(const void* p, size_t n) { auto* c = static_cast<const uint8_t*>(p); d.insert(d.end(), c, c + n); }
    void str0(const std::string& s) { bytes(s.data(), s.size()); u8(0); }
    void align2() { if (d.size() & 1) u8(0); }
    void put16(size_t o, uint16_t v) { d[o] = uint8_t(v >> 8); d[o + 1] = uint8_t(v); }
    void put32(size_t o, uint32_t v) { put16(o, uint16_t(v >> 16)); put16(o + 2, uint16_t(v)); }
};

bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
bool writeText(const std::string& path, const std::string& text);
bool fileExists(const std::string& path);

// path helpers
std::string pathStem(const std::string& path);      // "/a/b/FOO.RSC" -> "/a/b/FOO"
std::string pathBase(const std::string& path);      // "/a/b/FOO.RSC" -> "FOO.RSC"
std::string pathDir(const std::string& path);       // "/a/b/FOO.RSC" -> "/a/b"
std::string pathExt(const std::string& path);       // ".RSC" (as written)
// Find an existing file "<stem>.<ext>" trying lower and upper case extensions.
std::string findSibling(const std::string& path, const char* ext);
std::string toUpper(std::string s);
std::string toLower(std::string s);

// Atari ST character set <-> UTF-8 (the model stores raw Atari bytes).
std::string atariToUtf8(const std::string& s);
std::string utf8ToAtari(const std::string& s);
uint32_t atariToUnicode(uint8_t c);
int unicodeToAtari(uint32_t cp);   // -1 if not representable

} // namespace base
