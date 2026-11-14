#include "base/png.h"
#include "base/bytes.h"

namespace base {

static uint32_t crcTable[256];
static void initCrc()
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
static uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu)
{
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void chunk(Writer& w, const char* type, const std::vector<uint8_t>& data)
{
    w.u32(uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    w.bytes(td.data(), td.size());
    w.u32(crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

bool writePng(const std::string& path, int width, int height, const std::vector<uint32_t>& px)
{
    initCrc();
    Writer w;
    const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    w.bytes(sig, 8);
    Writer ih;
    ih.u32(width); ih.u32(height); ih.u8(8); ih.u8(2); ih.u8(0); ih.u8(0); ih.u8(0);
    chunk(w, "IHDR", ih.d);

    std::vector<uint8_t> raw;
    raw.reserve(size_t(height) * (width * 3 + 1));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        for (int x = 0; x < width; x++) {
            uint32_t c = px[size_t(y) * width + x];
            raw.push_back(uint8_t(c >> 16)); raw.push_back(uint8_t(c >> 8)); raw.push_back(uint8_t(c));
        }
    }
    // zlib stream with stored blocks
    Writer z;
    z.u8(0x78); z.u8(0x01);
    size_t off = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    do {
        size_t n = raw.size() - off; if (n > 65535) n = 65535;
        bool last = off + n == raw.size();
        z.u8(last ? 1 : 0);
        z.u8(uint8_t(n)); z.u8(uint8_t(n >> 8)); z.u8(uint8_t(~n)); z.u8(uint8_t((~n) >> 8));
        z.bytes(raw.data() + off, n);
        off += n;
    } while (off < raw.size());
    z.u32((b << 16) | a);
    chunk(w, "IDAT", z.d);
    chunk(w, "IEND", {});
    return writeFile(path, w.d);
}

} // namespace base
