// Software canvas for the designer UI.  Coordinates are logical pixels;
// every primitive is scaled by the integer output scale.  Text uses the
// Atari 8x16 system font (Atari character set).
#pragma once
#include "gem/vdi.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

using gem::Rect;

namespace col {
constexpr uint32_t bg = 0xD4D4D4, panel = 0xECECEC, field = 0xFFFFFF, text = 0x000000, dim = 0x6E6E6E,
                   line = 0x000000, light = 0xB8B8B8, hover = 0xC4D4F0, selBg = 0x000000, selText = 0xFFFFFF,
                   accent = 0xE4007C, accent2 = 0x0060E0, warn = 0xC00000, header = 0xFFFFFF, disabled = 0x9A9A9A,
                   grid = 0x00A0A0;
}

class Canvas {
public:
    void begin(uint32_t* px, int pw, int ph, int stride, int scale);
    int w() const { return pw_ / s_; }
    int h() const { return ph_ / s_; }
    int scale() const { return s_; }

    void fill(const Rect& r, uint32_t c);
    void blend(const Rect& r, uint32_t c, int alpha);    // alpha 0..255
    void frame(const Rect& r, uint32_t c, int t = 1);
    void dashed(const Rect& r, uint32_t c1, uint32_t c2, int phase = 0);
    void hline(int x1, int x2, int y, uint32_t c);
    void vline(int x, int y1, int y2, uint32_t c);
    void line(int x1, int y1, int x2, int y2, uint32_t c);
    // Atari charset text, top-left at (x,y), clipped to maxW if > 0.
    int text(int x, int y, const std::string& s, uint32_t c, int maxW = -1);
    void textCentered(const Rect& r, const std::string& s, uint32_t c);
    static int textW(const std::string& s) { return int(s.size()) * 8; }
    static constexpr int lineH = 16;

    void pushClip(const Rect& r);
    void popClip();
    const Rect& clip() const { return clip_; }

    // Nearest neighbour blit of an emulated Atari screen; each Atari pixel
    // becomes zx*zy logical pixels.
    void blitScreen(const gem::Screen& s, int dx, int dy, int zx, int zy);
    // Indexed bitmap preview helper
    void pixel(int x, int y, uint32_t c) { fill(Rect(x, y, 1, 1), c); }
    // Physical pixel access (for previews at true 1:1 size)
    void physFill(int px, int py, int pw, int ph, uint32_t c);

private:
    uint32_t* px_ = nullptr;
    int pw_ = 0, ph_ = 0, stride_ = 0, s_ = 1;
    Rect clip_;
    std::vector<Rect> stack_;
};

} // namespace ui
