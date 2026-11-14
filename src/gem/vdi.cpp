#include "gem/vdi.h"
#include <algorithm>
#include <cstdlib>

namespace gem {

Rect Rect::intersect(const Rect& o) const
{
    int x1 = std::max(x, o.x), y1 = std::max(y, o.y);
    int x2 = std::min(x + w, o.x + o.w), y2 = std::min(y + h, o.y + o.h);
    if (x2 <= x1 || y2 <= y1) return Rect(x1, y1, 0, 0);
    return Rect(x1, y1, x2 - x1, y2 - y1);
}

Rect Rect::unite(const Rect& o) const
{
    if (empty()) return o;
    if (o.empty()) return *this;
    int x1 = std::min(x, o.x), y1 = std::min(y, o.y);
    int x2 = std::max(x + w, o.x + o.w), y2 = std::max(y + h, o.y + o.h);
    return Rect(x1, y1, x2 - x1, y2 - y1);
}

// VDI colour index -> pixel value for 16 colour modes
static const uint8_t kVdiToPen16[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
static const uint8_t kVdiToPen4[4] = { 0, 3, 1, 2 };

// VDI fill dither patterns (vsf_style for FIS_PATTERN 1..8), 4 lines each
static const uint16_t kDither[8][4] = {
    { 0x0000, 0x4444, 0x0000, 0x1111 },
    { 0x0000, 0x5555, 0x0000, 0x5555 },
    { 0x8888, 0x5555, 0x2222, 0x5555 },
    { 0xAAAA, 0x5555, 0xAAAA, 0x5555 },
    { 0xAAAA, 0xDDDD, 0xAAAA, 0x7777 },
    { 0xAAAA, 0xFFFF, 0xAAAA, 0xFFFF },
    { 0xEEEE, 0xFFFF, 0xBBBB, 0xFFFF },
    { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF },
};

void Screen::init(int width, int height, int nplanes, const uint32_t* palette, int npal)
{
    w = width; h = height; planes = nplanes;
    px.assign(size_t(w) * h, 0);
    for (int i = 0; i < 256; i++) pal[i] = i < npal ? palette[i] : 0;
    resetClip();
}

int Screen::pen(int c) const
{
    int n = ncolors();
    if (c < 0 || c >= n) c = BLACK;   // VDI maps invalid colours to 1
    switch (planes) {
    case 1: return c ? 1 : 0;
    case 2: return kVdiToPen4[c];
    case 4: return kVdiToPen16[c];
    default:
        if (c < 16) return c == BLACK ? 255 : kVdiToPen16[c];
        if (c == 255) return 15;
        return c;
    }
}

int Screen::vdiOfPen(int p) const
{
    for (int c = 0; c < std::min(ncolors(), 256); c++)
        if (pen(c) == p) return c;
    return p;
}

inline void Screen::plot(int x, int y, bool bit, int mode, int p)
{
    uint8_t& d = px[size_t(y) * w + x];
    switch (mode) {
    case MD_REPLACE: d = uint8_t(bit ? p : 0); break;
    case MD_TRANS: if (bit) d = uint8_t(p); break;
    case MD_XOR: if (bit) d = uint8_t(d ^ planeMask()); break;
    case MD_ERASE: if (!bit) d = uint8_t(p); break;
    }
}

void Screen::fill(int x, int y, int ww, int hh, int mode, int interior, int style, int color)
{
    Rect r = Rect(x, y, ww, hh).intersect(clip_);
    if (r.empty()) return;
    int p = pen(color);
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        uint16_t pat;
        if (interior == FIS_SOLID) pat = 0xFFFF;
        else if (interior == FIS_HOLLOW) pat = 0x0000;
        else {
            int s = std::clamp(style, 1, 8);
            pat = kDither[s - 1][yy & 3];
        }
        for (int xx = r.x; xx < r.x + r.w; xx++)
            plot(xx, yy, (pat >> (15 - (xx & 15))) & 1, mode, p);
    }
}

void Screen::line(int x1, int y1, int x2, int y2, int mode, int color)
{
    int p = pen(color);
    int dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (clip_.contains(x1, y1)) plot(x1, y1, true, mode, p);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void Screen::box(const Rect& r, int mode, int color)
{
    if (r.w <= 0 || r.h <= 0) return;
    int x2 = r.x + r.w - 1, y2 = r.y + r.h - 1;
    // gsx_box draws a closed polyline; XOR mode must not touch corners twice
    line(r.x, r.y, x2, r.y, mode, color);
    if (y2 > r.y) line(r.x, y2, x2, y2, mode, color);
    if (y2 - r.y > 1) {
        line(r.x, r.y + 1, r.x, y2 - 1, mode, color);
        if (x2 > r.x) line(x2, r.y + 1, x2, y2 - 1, mode, color);
    }
}

void Screen::text(const SysFont& f, int x, int y, const char* s, int n, int mode, int color)
{
    int p = pen(color);
    for (int i = 0; i < n; i++, x += f.cw) {
        unsigned char c = (unsigned char)s[i];
        for (int ry = 0; ry < f.ch; ry++) {
            int yy = y + ry;
            if (yy < clip_.y || yy >= clip_.y + clip_.h) continue;
            unsigned char bits = f.row(c, ry);
            for (int rx = 0; rx < f.cw; rx++) {
                int xx = x + rx;
                if (xx < clip_.x || xx >= clip_.x + clip_.w) continue;
                plot(xx, yy, (bits >> (7 - rx)) & 1, mode, p);
            }
        }
    }
}

void Screen::bltMono(const uint16_t* data, size_t nwords, int wpr, int sx, int sy, int dx, int dy, int ww, int hh,
                     int mode, int fg, int bg)
{
    int pf = pen(fg), pb = pen(bg);
    for (int y = 0; y < hh; y++) {
        int yy = dy + y;
        if (yy < clip_.y || yy >= clip_.y + clip_.h) continue;
        for (int x = 0; x < ww; x++) {
            int xx = dx + x;
            if (xx < clip_.x || xx >= clip_.x + clip_.w) continue;
            int bx = sx + x, by = sy + y;
            size_t i = size_t(by) * wpr + bx / 16;
            bool bit = bx >= 0 && by >= 0 && bx < wpr * 16 && i < nwords && ((data[i] >> (15 - (bx & 15))) & 1);
            uint8_t& d = px[size_t(yy) * w + xx];
            switch (mode) {
            case MD_REPLACE: d = uint8_t(bit ? pf : pb); break;
            case MD_TRANS: if (bit) d = uint8_t(pf); break;
            case MD_XOR: if (bit) d = uint8_t(d ^ planeMask()); break;
            case MD_ERASE: if (!bit) d = uint8_t(pf); break;
            }
        }
    }
}

void Screen::bltPlanesOr(const std::vector<uint16_t>& pd, int wpr, int hh, int np, int dx, int dy)
{
    size_t plane = size_t(wpr) * hh;
    for (int y = 0; y < hh; y++) {
        int yy = dy + y;
        if (yy < clip_.y || yy >= clip_.y + clip_.h) continue;
        for (int x = 0; x < wpr * 16; x++) {
            int xx = dx + x;
            if (xx < clip_.x || xx >= clip_.x + clip_.w) continue;
            int v = 0;
            for (int p = 0; p < np; p++) {
                size_t i = p * plane + size_t(y) * wpr + x / 16;
                if (i < pd.size() && ((pd[i] >> (15 - (x & 15))) & 1)) v |= 1 << p;
            }
            px[size_t(yy) * w + xx] |= uint8_t(v & planeMask());
        }
    }
}

} // namespace gem
