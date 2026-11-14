// A minimal re-implementation of the parts of the Atari VDI that the AES
// uses to draw objects.  Pixels are stored as *hardware pixel values*
// (bitplane values) exactly like screen memory, so XOR and colour
// mapping behave like on the real machine.
#pragma once
#include "gem/font.h"
#include <cstdint>
#include <string>
#include <vector>

namespace gem {

enum { MD_REPLACE = 1, MD_TRANS = 2, MD_XOR = 3, MD_ERASE = 4 };
enum { FIS_HOLLOW = 0, FIS_SOLID = 1, FIS_PATTERN = 2 };
enum {
    WHITE = 0, BLACK = 1, RED = 2, GREEN = 3, BLUE = 4, CYAN = 5, YELLOW = 6, MAGENTA = 7,
    LWHITE = 8, LBLACK = 9, LRED = 10, LGREEN = 11, LBLUE = 12, LCYAN = 13, LYELLOW = 14, LMAGENTA = 15
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    Rect inset(int d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }
    Rect intersect(const Rect& o) const;
    Rect unite(const Rect& o) const;
};

class Screen {
public:
    int w = 0, h = 0, planes = 1;
    std::vector<uint8_t> px;          // hardware pixel values
    uint32_t pal[256] = {};           // hardware value -> 0xRRGGBB

    void init(int width, int height, int nplanes, const uint32_t* palette, int npal);
    int ncolors() const { return 1 << planes; }
    int planeMask() const { return ncolors() - 1; }
    int pen(int vdiColor) const;      // VDI colour index -> hardware value
    int vdiOfPen(int pen) const;      // inverse mapping (for editors)

    void setClip(const Rect& r) { clip_ = r.intersect(Rect(0, 0, w, h)); }
    void resetClip() { clip_ = Rect(0, 0, w, h); }
    const Rect& clip() const { return clip_; }

    // VDI style fill: style 1..8 for FIS_PATTERN (dither patterns), aligned to
    // screen coordinates like the real VDI.
    void fill(int x, int y, int ww, int hh, int mode, int interior, int style, int color);
    void line(int x1, int y1, int x2, int y2, int mode, int color);
    void box(const Rect& r, int mode, int color);    // 1 pixel outline (gsx_box)
    // Text, top aligned (as the AES uses it).
    void text(const SysFont& f, int x, int y, const char* s, int n, int mode, int color);
    // vrt_cpyfm: monochrome source to screen
    void bltMono(const uint16_t* data, size_t nwords, int wpr, int sx, int sy, int dx, int dy, int ww, int hh,
                 int mode, int fg, int bg);
    // colour source in device independent planes, combined with S_OR_D
    void bltPlanesOr(const std::vector<uint16_t>& planesData, int wpr, int hh, int nplanes, int dx, int dy);

    uint32_t rgbAt(int x, int y) const { return pal[px[size_t(y) * w + x]]; }

private:
    Rect clip_;
    inline void plot(int x, int y, bool bit, int mode, int pen);
};

} // namespace gem
