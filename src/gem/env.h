// Target system description: which AES version and which screen mode the
// resource is rendered for.
#pragma once
#include "gem/font.h"
#include <cstdint>
#include <string>
#include <vector>

namespace gem {

struct AesProfile {
    const char* name;      // "TOS 1.04 (AES 1.40)"
    int version;           // 0x0140
    // feature switches derived from the version
    bool colourIcons() const { return version >= 0x0330; }
    bool submenus() const { return version >= 0x0330; }
    bool threeD() const { return version >= 0x0340; }
};

enum class Machine { ST, TT, Falcon };

struct Resolution {
    const char* name;      // "ST High 640x400"
    int w, h, planes;
    int cellW, cellH;      // system font cell (gl_wchar/gl_hchar)
    int aspectX, aspectY;  // display pixel aspect (ST medium: 1x2)
    Machine machine;
};

const std::vector<AesProfile>& aesProfiles();
const std::vector<Resolution>& resolutions();

struct Env {
    AesProfile aes;
    Resolution res;
    const SysFont* ibm = nullptr;
    const SysFont* small = nullptr;
    uint32_t palette[256] = {};
    int npal = 2;

    // AES 3D variables (objc_sysvar defaults)
    bool indTxtMove = false, indColChange = true, actTxtMove = true, actColChange = false;
    int indButCol = 0, actButCol = 0, backgrCol = 0;

    int wbox() const { return res.cellW + 2; }   // gl_wbox
    int hbox() const { return res.cellH + 3; }   // gl_hbox (menu bar height)
    int ncolors() const { return 1 << res.planes; }
    bool use3D() const { return aes.threeD() && ncolors() >= 16; }

    static Env make(int aesIndex, int resIndex);
};

// Alert icons (32x32): 0 = note (!), 1 = question (?), 2 = stop.
const uint16_t* alertIcon(int i);
// Replace the built-in alert icons (e.g. with ones taken from a TOS ROM).
void setAlertIcons(const uint16_t icons[3][64]);

} // namespace gem
