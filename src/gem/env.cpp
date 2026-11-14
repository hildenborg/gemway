#include "gem/env.h"
#include <cstring>

namespace gem {

extern const uint32_t kTTPalette[256];
extern const uint32_t kFalconPalette[256];
extern const uint16_t kAlertIcons[3][64];

static uint16_t gAlertIcons[3][64];
static bool gAlertInit = false;

const uint16_t* alertIcon(int i)
{
    if (!gAlertInit) { memcpy(gAlertIcons, kAlertIcons, sizeof gAlertIcons); gAlertInit = true; }
    if (i < 0 || i > 2) i = 0;
    return gAlertIcons[i];
}

void setAlertIcons(const uint16_t icons[3][64])
{
    memcpy(gAlertIcons, icons, sizeof gAlertIcons);
    gAlertInit = true;
}

const std::vector<AesProfile>& aesProfiles()
{
    static const std::vector<AesProfile> v = {
        { "TOS 1.00/1.02 (AES 1.20)", 0x0120 },
        { "TOS 1.04/1.06/1.62 (AES 1.40)", 0x0140 },
        { "TOS 2.0x/3.0x (AES 3.00-3.20)", 0x0320 },
        { "TOS 4.0x Falcon (AES 3.40)", 0x0340 },
        { "MultiTOS (AES 4.10)", 0x0410 },
    };
    return v;
}

const std::vector<Resolution>& resolutions()
{
    static const std::vector<Resolution> v = {
        { "ST Low 320x200 16 col", 320, 200, 4, 8, 8, 1, 1, Machine::ST },
        { "ST Medium 640x200 4 col", 640, 200, 2, 8, 8, 1, 2, Machine::ST },
        { "ST High 640x400 mono", 640, 400, 1, 8, 16, 1, 1, Machine::ST },
        { "TT Medium 640x480 16 col", 640, 480, 4, 8, 16, 1, 1, Machine::TT },
        { "TT Low 320x480 256 col", 320, 480, 8, 8, 16, 2, 1, Machine::TT },
        { "TT High 1280x960 mono", 1280, 960, 1, 8, 16, 1, 1, Machine::TT },
        { "Falcon 640x480 16 col", 640, 480, 4, 8, 16, 1, 1, Machine::Falcon },
        { "Falcon 640x480 256 col", 640, 480, 8, 8, 16, 1, 1, Machine::Falcon },
        { "Falcon 640x400 mono", 640, 400, 1, 8, 16, 1, 1, Machine::Falcon },
    };
    return v;
}

static uint32_t st3(int v)   // ST 3-bit per channel register value 0x0RGB
{
    int r = (v >> 8) & 7, g = (v >> 4) & 7, b = v & 7;
    auto c = [](int x) { return uint32_t(x * 255 / 7); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

Env Env::make(int aesIndex, int resIndex)
{
    Env e;
    const auto& ap = aesProfiles();
    const auto& rs = resolutions();
    e.aes = ap[(aesIndex >= 0 && aesIndex < (int)ap.size()) ? aesIndex : 1];
    e.res = rs[(resIndex >= 0 && resIndex < (int)rs.size()) ? resIndex : 2];
    e.ibm = e.res.cellH >= 16 ? &font_8x16 : &font_8x8;
    e.small = &font_6x6;

    memset(e.palette, 0, sizeof e.palette);
    switch (e.res.planes) {
    case 1:
        e.palette[0] = 0xFFFFFF; e.palette[1] = 0x000000; e.npal = 2;
        break;
    case 2:
        e.palette[0] = st3(0x777); e.palette[1] = st3(0x700); e.palette[2] = st3(0x070); e.palette[3] = st3(0x000);
        e.npal = 4;
        break;
    case 4:
        if (e.res.machine == Machine::ST) {
            static const int stpal[16] = { 0x777, 0x700, 0x070, 0x770, 0x007, 0x707, 0x077, 0x555,
                                           0x333, 0x733, 0x373, 0x773, 0x337, 0x737, 0x377, 0x000 };
            for (int i = 0; i < 16; i++) e.palette[i] = st3(stpal[i]);
        } else {
            const uint32_t* p = e.res.machine == Machine::TT ? kTTPalette : kFalconPalette;
            for (int i = 0; i < 16; i++) e.palette[i] = p[i];
        }
        e.npal = 16;
        break;
    default: {
        const uint32_t* p = e.res.machine == Machine::TT ? kTTPalette : kFalconPalette;
        for (int i = 0; i < 256; i++) e.palette[i] = p[i];
        e.npal = 256;
        break;
    }
    }

    // objc_sysvar() defaults (AES 3.40+)
    int col = e.ncolors() <= 8 ? 0 /*WHITE*/ : 8 /*LWHITE*/;
    e.indButCol = e.actButCol = e.backgrCol = col;
    return e;
}

} // namespace gem
