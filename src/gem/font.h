// Atari system fonts (bitmap, one byte per glyph row, MSB = leftmost pixel).
#pragma once

namespace gem {
struct SysFont {
    int cw, ch, top;              // cell width/height, top line
    const unsigned char* data;    // 256 glyphs * ch rows
    unsigned char row(unsigned char c, int y) const { return data[c * ch + y]; }
};
extern const SysFont font_6x6, font_8x8, font_8x16;
} // namespace gem
