#include "ui/canvas.h"
#include "gem/font.h"
#include <algorithm>
#include <cstdlib>

namespace ui {

void Canvas::begin(uint32_t* px, int pw, int ph, int stride, int scale)
{
    px_ = px; pw_ = pw; ph_ = ph; stride_ = stride; s_ = std::max(1, scale);
    clip_ = Rect(0, 0, w(), h());
    stack_.clear();
}

void Canvas::physFill(int x, int y, int ww, int hh, uint32_t c)
{
    int cx1 = clip_.x * s_, cy1 = clip_.y * s_, cx2 = (clip_.x + clip_.w) * s_, cy2 = (clip_.y + clip_.h) * s_;
    int x1 = std::max(x, cx1), y1 = std::max(y, cy1);
    int x2 = std::min(x + ww, cx2), y2 = std::min(y + hh, cy2);
    x1 = std::max(x1, 0); y1 = std::max(y1, 0); x2 = std::min(x2, pw_); y2 = std::min(y2, ph_);
    for (int yy = y1; yy < y2; yy++) {
        uint32_t* row = px_ + size_t(yy) * stride_;
        std::fill(row + x1, row + x2, c | 0xFF000000u);
    }
}

void Canvas::fill(const Rect& r, uint32_t c)
{
    Rect k = r.intersect(clip_);
    if (k.empty()) return;
    physFill(k.x * s_, k.y * s_, k.w * s_, k.h * s_, c);
}

void Canvas::blend(const Rect& r, uint32_t c, int a)
{
    Rect k = r.intersect(clip_);
    if (k.empty()) return;
    int x1 = std::max(0, k.x * s_), y1 = std::max(0, k.y * s_);
    int x2 = std::min(pw_, (k.x + k.w) * s_), y2 = std::min(ph_, (k.y + k.h) * s_);
    uint32_t cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
    for (int y = y1; y < y2; y++) {
        uint32_t* row = px_ + size_t(y) * stride_;
        for (int x = x1; x < x2; x++) {
            uint32_t d = row[x];
            uint32_t r_ = (((d >> 16) & 255) * (255 - a) + cr * a) / 255;
            uint32_t g_ = (((d >> 8) & 255) * (255 - a) + cg * a) / 255;
            uint32_t b_ = ((d & 255) * (255 - a) + cb * a) / 255;
            row[x] = 0xFF000000u | (r_ << 16) | (g_ << 8) | b_;
        }
    }
}

void Canvas::frame(const Rect& r, uint32_t c, int t)
{
    for (int i = 0; i < t; i++) {
        Rect k = r.inset(i);
        if (k.w <= 0 || k.h <= 0) break;
        fill(Rect(k.x, k.y, k.w, 1), c);
        fill(Rect(k.x, k.y + k.h - 1, k.w, 1), c);
        fill(Rect(k.x, k.y, 1, k.h), c);
        fill(Rect(k.x + k.w - 1, k.y, 1, k.h), c);
    }
}

void Canvas::dashed(const Rect& r, uint32_t c1, uint32_t c2, int phase)
{
    if (r.w <= 0 || r.h <= 0) return;
    auto pc = [&](int i) { return ((i + phase) / 3) & 1 ? c2 : c1; };
    for (int x = 0; x < r.w; x++) {
        fill(Rect(r.x + x, r.y, 1, 1), pc(x));
        fill(Rect(r.x + x, r.y + r.h - 1, 1, 1), pc(x));
    }
    for (int y = 0; y < r.h; y++) {
        fill(Rect(r.x, r.y + y, 1, 1), pc(y));
        fill(Rect(r.x + r.w - 1, r.y + y, 1, 1), pc(y));
    }
}

void Canvas::hline(int x1, int x2, int y, uint32_t c) { if (x2 < x1) std::swap(x1, x2); fill(Rect(x1, y, x2 - x1 + 1, 1), c); }
void Canvas::vline(int x, int y1, int y2, uint32_t c) { if (y2 < y1) std::swap(y1, y2); fill(Rect(x, y1, 1, y2 - y1 + 1), c); }

void Canvas::line(int x1, int y1, int x2, int y2, uint32_t c)
{
    int dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx + dy;
    for (;;) {
        fill(Rect(x1, y1, 1, 1), c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

int Canvas::text(int x, int y, const std::string& str, uint32_t c, int maxW)
{
    const gem::SysFont& f = gem::font_8x16;
    int n = int(str.size());
    if (maxW >= 0 && n * f.cw > maxW) n = std::max(0, maxW / f.cw);
    int cx1 = clip_.x * s_, cy1 = clip_.y * s_, cx2 = (clip_.x + clip_.w) * s_, cy2 = (clip_.y + clip_.h) * s_;
    cx1 = std::max(cx1, 0); cy1 = std::max(cy1, 0); cx2 = std::min(cx2, pw_); cy2 = std::min(cy2, ph_);
    uint32_t cc = c | 0xFF000000u;
    for (int i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)str[i];
        int gx = (x + i * f.cw) * s_;
        if (gx >= cx2 || gx + f.cw * s_ <= cx1) continue;
        for (int ry = 0; ry < f.ch; ry++) {
            unsigned char bits = f.row(ch, ry);
            if (!bits) continue;
            int py = (y + ry) * s_;
            for (int rx = 0; rx < f.cw; rx++) {
                if (!((bits >> (7 - rx)) & 1)) continue;
                int px = gx + rx * s_;
                for (int sy = 0; sy < s_; sy++) {
                    int yy = py + sy;
                    if (yy < cy1 || yy >= cy2) continue;
                    uint32_t* row = px_ + size_t(yy) * stride_;
                    for (int sx = 0; sx < s_; sx++) {
                        int xx = px + sx;
                        if (xx >= cx1 && xx < cx2) row[xx] = cc;
                    }
                }
            }
        }
    }
    return n * f.cw;
}

void Canvas::textCentered(const Rect& r, const std::string& s, uint32_t c)
{
    int tw = textW(s);
    int maxc = std::max(0, r.w / 8);
    std::string t = s;
    if ((int)t.size() > maxc) { t = t.substr(0, maxc); tw = textW(t); }
    text(r.x + (r.w - tw) / 2, r.y + (r.h - lineH) / 2, t, c);
}

void Canvas::pushClip(const Rect& r)
{
    stack_.push_back(clip_);
    clip_ = clip_.intersect(r);
}

void Canvas::popClip()
{
    if (!stack_.empty()) { clip_ = stack_.back(); stack_.pop_back(); }
}

void Canvas::blitScreen(const gem::Screen& scr, int dx, int dy, int zx, int zy)
{
    int fx = zx * s_, fy = zy * s_;
    int cx1 = std::max(clip_.x * s_, 0), cy1 = std::max(clip_.y * s_, 0);
    int cx2 = std::min((clip_.x + clip_.w) * s_, pw_), cy2 = std::min((clip_.y + clip_.h) * s_, ph_);
    int ox = dx * s_, oy = dy * s_;
    int x1 = std::max(cx1, ox), y1 = std::max(cy1, oy);
    int x2 = std::min(cx2, ox + scr.w * fx), y2 = std::min(cy2, oy + scr.h * fy);
    for (int y = y1; y < y2; y++) {
        int sy = (y - oy) / fy;
        const uint8_t* src = &scr.px[size_t(sy) * scr.w];
        uint32_t* row = px_ + size_t(y) * stride_;
        for (int x = x1; x < x2; x++) row[x] = scr.pal[src[(x - ox) / fx]] | 0xFF000000u;
    }
}

} // namespace ui
