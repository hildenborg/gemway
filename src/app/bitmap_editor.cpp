// Pixel editor for images (BITBLK), icons (ICONBLK) and colour icons (CICONBLK).
#include "app/app.h"
#include "base/bytes.h"
#include <algorithm>
#include <functional>

namespace app {

using namespace rsc;
using namespace platform;
namespace col = ui::col;

namespace {

enum Tool { PENCIL, LINE, RECT, FRECT, FILL, PICK };
const char* kToolNames[] = { "Pencil", "Line", "Rectangle", "Filled rect", "Flood fill", "Pick colour" };

// one editable "view": pixel values plus optional mask
struct Layer {
    std::vector<uint8_t> pix, mask;
};

struct BitmapEditor : Modal {
    enum Mode { IMAGE, ICON, CICON } mode = IMAGE;
    ItemKind kind;
    int item = -1, obj = -1;

    int w = 32, h = 32;
    int bitColor = 1;
    IconBlk icon;                 // for icon modes (geometry/colours)
    Layer mono;                   // mono data/mask (image: pix only)
    std::vector<CIcon> cicons;    // colour resolutions (stored representation)
    std::vector<Layer> colLayers, selLayers;
    int view = 0;                 // 0 = mono, 1.. = colour resolution (view-1), selected if viewSel
    bool viewSel = false;
    int target = 0;               // 0 = pixels, 1 = mask
    int tool = PENCIL;
    int colour = 1;               // VDI colour index (colour views) / 1 = set (mono)
    int zoom = 12;
    int newW = 32, newH = 32;
    std::vector<std::pair<Layer, Layer>> undo;  // (mono snapshot, current layer snapshot) simplified: full state
    struct Snapshot { Layer mono; std::vector<Layer> col, sel; std::vector<CIcon> cicons; };
    std::vector<Snapshot> undoStack;
    bool dragging = false;
    int sx = 0, sy = 0, ex = 0, ey = 0;
    int dragButton = 0;

    // ------------------------------------------------------------ helpers
    bool isColourView() const { return mode == CICON && view > 0; }
    int planes() const { return isColourView() ? cicons[view - 1].planes : 1; }
    Layer& cur()
    {
        if (!isColourView()) return mono;
        return viewSel ? selLayers[view - 1] : colLayers[view - 1];
    }
    int penFor(int vdi) const
    {
        int p = planes();
        if (p == 1) return vdi ? 1 : 0;
        gem::Screen tmp;
        tmp.planes = p;
        return tmp.pen(vdi);
    }

    void snapshot()
    {
        undoStack.push_back({ mono, colLayers, selLayers, cicons });
        if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
    }
    void restore()
    {
        if (undoStack.empty()) return;
        Snapshot& s = undoStack.back();
        mono = s.mono; colLayers = s.col; selLayers = s.sel; cicons = s.cicons;
        undoStack.pop_back();
        if (view > (int)cicons.size()) view = 0;
    }

    static Layer fromBitmaps(const Bitmap* data, const Bitmap* mask, int w, int h)
    {
        Layer l;
        l.pix.assign(size_t(w) * h, 0);
        l.mask.assign(size_t(w) * h, 1);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (data) l.pix[size_t(y) * w + x] = data->get(x, y);
                if (mask) l.mask[size_t(y) * w + x] = mask->get(x, y);
            }
        return l;
    }
    static Bitmap toBitmap(const std::vector<uint8_t>& v, int w, int h)
    {
        Bitmap b;
        b.resize(w, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (v[size_t(y) * w + x]) b.set(x, y, true);
        return b;
    }
    Layer fromCIcon(const CIcon& c, bool sel) const
    {
        Layer l;
        int wpr = (w + 15) / 16;
        l.pix.assign(size_t(w) * h, 0);
        l.mask.assign(size_t(w) * h, 0);
        const std::vector<uint16_t>& d = sel ? c.sel : c.col;
        const std::vector<uint16_t>& m = sel ? c.selMask : c.colMask;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                l.pix[size_t(y) * w + x] = uint8_t(c.pixel(d, wpr, h, x, y));
                size_t i = size_t(y) * wpr + x / 16;
                l.mask[size_t(y) * w + x] = i < m.size() && (m[i] & (0x8000 >> (x & 15)));
            }
        return l;
    }
    void toCIcon(const Layer& l, CIcon& c, bool sel) const
    {
        int wpr = (w + 15) / 16;
        std::vector<uint16_t> d(size_t(wpr) * h * c.planes, 0), m(size_t(wpr) * h, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                c.setPixel(d, wpr, h, x, y, l.pix[size_t(y) * w + x]);
                if (l.mask[size_t(y) * w + x]) m[size_t(y) * wpr + x / 16] |= uint16_t(0x8000 >> (x & 15));
            }
        if (sel) { c.sel = d; c.selMask = m; }
        else { c.col = d; c.colMask = m; }
    }

    void load(App& a)
    {
        kind = a.selKind;
        item = a.selItem;
        if (kind == ItemKind::Image) {
            const BitBlk& b = a.doc.images[item].bit;
            mode = IMAGE;
            w = b.img.w; h = b.img.h; bitColor = b.color;
            mono = fromBitmaps(&b.img, nullptr, w, h);
        } else {
            obj = a.primaryObj();
            const Object& o = a.curTree()->obj[obj];
            if (o.baseType() == G_IMAGE) {
                mode = IMAGE;
                w = o.bit.img.w; h = o.bit.img.h; bitColor = o.bit.color;
                mono = fromBitmaps(&o.bit.img, nullptr, w, h);
            } else {
                mode = o.baseType() == G_CICON ? CICON : ICON;
                icon = o.icon;
                w = (icon.wicon + 15) & ~15; h = icon.hicon;
                mono = fromBitmaps(&icon.data, &icon.mask, w, h);
                cicons = o.cicons;
                for (auto& c : cicons) {
                    colLayers.push_back(fromCIcon(c, false));
                    selLayers.push_back(c.hasSel ? fromCIcon(c, true) : fromCIcon(c, false));
                }
                if (mode == CICON && !cicons.empty()) view = 1;
            }
        }
        newW = w; newH = h;
        if (w <= 0 || h <= 0) { w = h = newW = newH = 16; mono.pix.assign(256, 0); mono.mask.assign(256, 1); }
        colour = isColourView() ? 1 : 1;
    }

    void storeColourLayers()
    {
        for (size_t i = 0; i < cicons.size(); i++) {
            toCIcon(colLayers[i], cicons[i], false);
            if (cicons[i].hasSel) toCIcon(selLayers[i], cicons[i], true);
            else { cicons[i].sel.clear(); cicons[i].selMask.clear(); }
        }
    }

    void apply(App& a)
    {
        a.checkpoint();
        storeColourLayers();
        if (kind == ItemKind::Image) {
            BitBlk& b = a.doc.images[item].bit;
            b.img = toBitmap(mono.pix, w, h);
            b.color = int16_t(bitColor);
            return;
        }
        Tree* t = a.curTree();
        if (!t || obj < 0 || obj >= (int)t->obj.size()) return;
        Object& o = t->obj[obj];
        if (mode == IMAGE) {
            o.bit.img = toBitmap(mono.pix, w, h);
            o.bit.color = int16_t(bitColor);
        } else {
            icon.data = toBitmap(mono.pix, w, h);
            icon.mask = toBitmap(mono.mask, w, h);
            icon.wicon = int16_t(w);
            icon.hicon = int16_t(h);
            o.icon = icon;
            if (mode == CICON) o.cicons = cicons;
        }
        a.setStatus("Bitmap updated");
    }

    void resize(int nw, int nh)
    {
        nw = std::clamp((nw + 15) & ~15, 16, 512);
        nh = std::clamp(nh, 1, 512);
        snapshot();
        auto rl = [&](Layer& l) {
            Layer n;
            n.pix.assign(size_t(nw) * nh, 0);
            n.mask.assign(size_t(nw) * nh, mode == IMAGE ? 1 : 0);
            for (int y = 0; y < std::min(h, nh); y++)
                for (int x = 0; x < std::min(w, nw); x++) {
                    n.pix[size_t(y) * nw + x] = l.pix[size_t(y) * w + x];
                    n.mask[size_t(y) * nw + x] = l.mask.empty() ? 1 : l.mask[size_t(y) * w + x];
                }
            l = n;
        };
        rl(mono);
        for (auto& l : colLayers) rl(l);
        for (auto& l : selLayers) rl(l);
        w = nw; h = nh;
    }

    // ------------------------------------------------------------ pixel ops
    std::vector<uint8_t>& tgt() { return target == 1 && mode != IMAGE ? cur().mask : cur().pix; }
    int valueFor(int button) const
    {
        if (target == 1 || !isColourView()) return button == 0 ? 1 : 0;
        return button == 0 ? penFor(colour) : 0;
    }
    void plot(int x, int y, int v)
    {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        tgt()[size_t(y) * w + x] = uint8_t(v);
        // drawing colour pixels also extends the mask (convenient default)
        if (isColourView() && target == 0 && v) cur().mask[size_t(y) * w + x] = 1;
    }
    void lineTo(int x0, int y0, int x1, int y1, int v)
    {
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx2 = x0 < x1 ? 1 : -1, sy2 = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            plot(x0, y0, v);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx2; }
            if (e2 <= dx) { err += dx; y0 += sy2; }
        }
    }
    void rect(int x0, int y0, int x1, int y1, int v, bool filled)
    {
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                if (filled || x == x0 || x == x1 || y == y0 || y == y1) plot(x, y, v);
    }
    void flood(int x, int y, int v)
    {
        auto& t = tgt();
        int from = t[size_t(y) * w + x];
        if (from == v) return;
        std::vector<std::pair<int, int>> st = { { x, y } };
        while (!st.empty()) {
            auto [cx, cy] = st.back();
            st.pop_back();
            if (cx < 0 || cy < 0 || cx >= w || cy >= h || t[size_t(cy) * w + cx] != from) continue;
            plot(cx, cy, v);
            st.push_back({ cx + 1, cy }); st.push_back({ cx - 1, cy });
            st.push_back({ cx, cy + 1 }); st.push_back({ cx, cy - 1 });
        }
    }
    void transform(int op)
    {
        snapshot();
        auto& t = tgt();
        std::vector<uint8_t> n = t;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int sxx = x, syy = y;
                switch (op) {
                case 0: n[size_t(y) * w + x] = 0; continue;                                   // clear
                case 1: n[size_t(y) * w + x] = isColourView() && target == 0 ? uint8_t((~t[size_t(y) * w + x]) & ((1 << planes()) - 1)) : !t[size_t(y) * w + x]; continue;
                case 2: sxx = w - 1 - x; break;                                               // flip h
                case 3: syy = h - 1 - y; break;                                               // flip v
                case 4: sxx = x + 1; break;                                                   // shift left
                case 5: sxx = x - 1; break;                                                   // shift right
                case 6: syy = y + 1; break;                                                   // shift up
                case 7: syy = y - 1; break;                                                   // shift down
                }
                n[size_t(y) * w + x] = (sxx >= 0 && syy >= 0 && sxx < w && syy < h) ? t[size_t(syy) * w + sxx] : 0;
            }
        t = n;
    }
    void maskFromData()
    {
        snapshot();
        Layer& l = cur();
        // everything not reachable from the border through empty pixels is inside
        std::vector<uint8_t> outside(size_t(w) * h, 0);
        std::vector<std::pair<int, int>> st;
        for (int x = 0; x < w; x++) { st.push_back({ x, 0 }); st.push_back({ x, h - 1 }); }
        for (int y = 0; y < h; y++) { st.push_back({ 0, y }); st.push_back({ w - 1, y }); }
        while (!st.empty()) {
            auto [x, y] = st.back();
            st.pop_back();
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            size_t i = size_t(y) * w + x;
            if (outside[i] || l.pix[i]) continue;
            outside[i] = 1;
            st.push_back({ x + 1, y }); st.push_back({ x - 1, y }); st.push_back({ x, y + 1 }); st.push_back({ x, y - 1 });
        }
        for (size_t i = 0; i < outside.size(); i++) l.mask[i] = !outside[i];
    }

    // ------------------------------------------------------------ colours
    uint32_t rgbOfPen(App& a, int pen) const
    {
        int p = planes();
        if (p == 1) return pen ? 0x000000 : 0xFFFFFF;
        if (a.screen.planes == p) return a.screen.pal[pen & 255];
        gem::Env e = gem::Env::make(a.set.aes, p == 8 ? 7 : p == 4 ? 3 : p == 2 ? 1 : 2);
        return e.palette[pen & 255];
    }
    uint32_t rgbOfVdi(App& a, int vdi) const
    {
        if (!isColourView()) {
            if (a.screen.ncolors() > vdi) return a.screen.pal[a.screen.pen(vdi)];
            return vdi ? 0 : 0xFFFFFF;
        }
        return rgbOfPen(a, penFor(vdi));
    }

    // ------------------------------------------------------------ preview
    void drawPreview(App& a, ui::Canvas& c, const Rect& area)
    {
        storeColourLayers();
        Tree t;
        Object root; root.type = G_BOX; root.spec = 0x00001100;
        int pw = std::max(w + 16, 72) * 2 + 8, ph = h + 24;
        if (mode != IMAGE) { pw = std::max(pw, (std::max<int>(icon.xtext + icon.wtext, icon.xicon + w) + 8) * 2 + 8); ph = std::max(ph, icon.ytext + icon.htext + 16); }
        root.w = makeRaw(0, pw); root.h = makeRaw(0, ph);
        rsc::addObject(t, -1, root);
        for (int k = 0; k < 2; k++) {
            Object o;
            int ox = 8 + k * (pw / 2);
            if (mode == IMAGE) {
                o.type = G_IMAGE;
                o.bit.img = toBitmap(mono.pix, w, h);
                o.bit.color = int16_t(bitColor);
                o.w = makeRaw(0, w); o.h = makeRaw(0, h);
            } else {
                o.type = mode == CICON ? G_CICON : G_ICON;
                o.icon = icon;
                o.icon.data = toBitmap(mono.pix, w, h);
                o.icon.mask = toBitmap(mono.mask, w, h);
                o.icon.wicon = int16_t(w); o.icon.hicon = int16_t(h);
                o.cicons = cicons;
                o.w = makeRaw(0, std::max<int>(icon.xtext + icon.wtext, icon.xicon + w));
                o.h = makeRaw(0, std::max<int>(icon.ytext + icon.htext, icon.yicon + h));
            }
            o.x = makeRaw(0, ox); o.y = makeRaw(0, 8);
            if (k == 1) o.state = SELECTED;
            rsc::addObject(t, 0, o);
        }
        gem::Screen s;
        s.init(pw, ph, a.env.res.planes, a.env.palette, a.env.npal);
        gem::Aes aes(s, a.env);
        gem::Layout L;
        aes.layout(t, 0, 0, L);
        aes.drawTree(t, L, 0, gem::MAX_DEPTH);
        int z = (pw * 2 * a.env.res.aspectX <= area.w && ph * 2 * a.env.res.aspectY <= area.h) ? 2 : 1;
        int zx = z * a.env.res.aspectX, zy = z * a.env.res.aspectY;
        c.pushClip(area);
        c.blitScreen(s, area.x, area.y, zx, zy);
        c.frame(Rect(area.x - 1, area.y - 1, pw * zx + 2, ph * zy + 2), col::line);
        c.popClip();
    }

    // ------------------------------------------------------------ draw
    bool draw(App& a, ui::Ui& u) override
    {
        ui::Canvas& c = u.c;
        int W = std::min(c.w() - 20, 1100), H = std::min(c.h() - 20, 720);
        Rect r((c.w() - W) / 2, (c.h() - H) / 2, W, H);
        c.fill(Rect(r.x + 5, r.y + 5, r.w, r.h), 0x303030);
        c.fill(r, col::panel);
        c.frame(r, col::line);
        static const char* titles[] = { "Image editor (BITBLK)", "Icon editor (ICONBLK)", "Colour icon editor (CICONBLK)" };
        Rect tr(r.x, r.y, r.w, 22);
        c.fill(tr, col::selBg);
        c.textCentered(tr, titles[mode], col::selText);

        // ---- left column: tools, colours, ops
        int lx = r.x + 10, ly = r.y + 32, lw = 170;
        c.text(lx, ly, "Tool", col::dim);
        ly += 20;
        for (int i = 0; i < 6; i++) {
            if (i == PICK && !isColourView()) continue;
            if (u.button(Rect(lx, ly, lw, 20), kToolNames[i], true, tool == i)) tool = i;
            ly += 22;
        }
        ly += 6;
        if (mode != IMAGE) {
            c.text(lx, ly, "Edit", col::dim);
            ly += 20;
            if (u.button(Rect(lx, ly, lw / 2 - 2, 20), isColourView() ? "Colour" : "Data", true, target == 0)) target = 0;
            if (u.button(Rect(lx + lw / 2 + 2, ly, lw / 2 - 2, 20), "Mask", true, target == 1)) target = 1;
            ly += 28;
        }
        if (isColourView() && target == 0) {
            c.text(lx, ly, "Colour (left button)", col::dim);
            ly += 20;
            int n = 16;
            for (int i = 0; i < n; i++) {
                Rect sw(lx + (i % 8) * 21, ly + (i / 8) * 21, 19, 19);
                c.fill(sw, rgbOfVdi(a, i));
                c.frame(sw, colour == i ? col::accent : col::line, colour == i ? 2 : 1);
                if (u.clicked(sw)) colour = i;
            }
            ly += 46;
            c.text(lx, ly, "Right button erases", col::dim);
            ly += 22;
        } else {
            c.text(lx, ly, "Left: set  Right: clear", col::dim);
            ly += 24;
        }
        c.text(lx, ly, "Operations", col::dim);
        ly += 20;
        static const char* ops[] = { "Clear", "Invert", "Flip H", "Flip V", "Shift left", "Shift right", "Shift up", "Shift down" };
        for (int i = 0; i < 8; i++) {
            if (u.button(Rect(lx + (i % 2) * (lw / 2 + 2), ly, lw / 2 - 2, 20), ops[i])) transform(i);
            if (i % 2) ly += 22;
        }
        if (mode != IMAGE) {
            if (u.button(Rect(lx, ly, lw, 20), "Mask from outline")) maskFromData();
            ly += 22;
            if (u.button(Rect(lx, ly, lw, 20), "Mask = all set")) { snapshot(); std::fill(cur().mask.begin(), cur().mask.end(), 1); }
            ly += 22;
        }
        if (u.button(Rect(lx, ly, lw, 20), "Undo  (Ctrl+Z)", !undoStack.empty())) restore();
        ly += 30;
        c.text(lx, ly, "Size (w x h)", col::dim);
        ly += 20;
        u.intField(u.id("be.w"), Rect(lx, ly, lw / 2 - 2, 20), newW, 16, 512);
        u.intField(u.id("be.h"), Rect(lx + lw / 2 + 2, ly, lw / 2 - 2, 20), newH, 1, 512);
        ly += 22;
        if (u.button(Rect(lx, ly, lw, 20), "Apply size", newW != w || newH != h)) resize(newW, newH);
        ly += 22;
        c.text(lx, ly, "Width: multiple of 16", col::dim);

        // ---- right column: views, preview
        int rx = r.x + r.w - 300, ry = r.y + 32, rw = 290;
        if (mode == CICON) {
            c.text(rx, ry, "Resolution", col::dim);
            ry += 20;
            std::vector<std::string> views = { "Monochrome (ICONBLK)" };
            for (auto& ci : cicons) views.push_back(std::to_string(ci.planes) + " planes (" + std::to_string(1 << ci.planes) + " colours)");
            int v = view;
            if (u.dropdown(u.id("be.view"), Rect(rx, ry, rw, 20), v, views)) { view = v; viewSel = false; }
            ry += 24;
            if (isColourView()) {
                CIcon& ci = cicons[view - 1];
                bool hs = ci.hasSel;
                if (u.checkbox(Rect(rx, ry, rw, 20), "Has separate SELECTED image", hs)) {
                    snapshot();
                    ci.hasSel = hs;
                    if (hs) selLayers[view - 1] = colLayers[view - 1];
                    if (!hs) viewSel = false;
                }
                ry += 22;
                if (ci.hasSel) {
                    if (u.button(Rect(rx, ry, rw / 2 - 2, 20), "Normal", true, !viewSel)) viewSel = false;
                    if (u.button(Rect(rx + rw / 2 + 2, ry, rw / 2 - 2, 20), "Selected", true, viewSel)) viewSel = true;
                    ry += 24;
                }
                if (u.button(Rect(rx, ry, rw, 20), "Remove this resolution")) {
                    snapshot();
                    cicons.erase(cicons.begin() + (view - 1));
                    colLayers.erase(colLayers.begin() + (view - 1));
                    selLayers.erase(selLayers.begin() + (view - 1));
                    view = 0;
                    viewSel = false;
                }
                ry += 24;
            }
            for (int p : { 4, 8 }) {
                bool have = false;
                for (auto& ci : cicons) if (ci.planes == p) have = true;
                if (have) continue;
                if (u.button(Rect(rx, ry, rw, 20), "Add " + std::to_string(p) + " plane version (from mono)")) {
                    snapshot();
                    CIcon ci;
                    ci.planes = p;
                    Layer l;
                    l.pix.assign(size_t(w) * h, 0);
                    l.mask = mono.mask;
                    gem::Screen tmp;
                    tmp.planes = p;
                    int fgv = (icon.ch >> 12) & 15, bgv = (icon.ch >> 8) & 15;
                    for (size_t i = 0; i < l.pix.size(); i++)
                        l.pix[i] = uint8_t(mono.pix[i] ? tmp.pen(fgv) : (mono.mask[i] ? tmp.pen(bgv) : 0));
                    cicons.push_back(ci);
                    colLayers.push_back(l);
                    selLayers.push_back(l);
                    view = (int)cicons.size();
                }
                ry += 24;
            }
            ry += 6;
        }
        if (mode == IMAGE) {
            c.text(rx, ry, "Colour", col::dim);
            ry += 20;
            std::vector<std::string> names = colourNames(16);
            u.dropdown(u.id("be.color"), Rect(rx, ry, rw, 20), bitColor, names);
            ry += 28;
        } else {
            int fg = (icon.ch >> 12) & 15, bg = (icon.ch >> 8) & 15;
            std::vector<std::string> names = colourNames(16);
            c.text(rx, ry + 2, "Foreground", col::dim);
            if (u.dropdown(u.id("be.fg"), Rect(rx + 100, ry, rw - 100, 20), fg, names)) icon.ch = uint16_t((icon.ch & 0x0FFF) | (fg << 12));
            ry += 24;
            c.text(rx, ry + 2, "Background", col::dim);
            if (u.dropdown(u.id("be.bg"), Rect(rx + 100, ry, rw - 100, 20), bg, names)) icon.ch = uint16_t((icon.ch & 0xF0FF) | (bg << 8));
            ry += 28;
        }
        c.text(rx, ry, "Preview (normal / selected)", col::dim);
        ry += 20;
        drawPreview(a, c, Rect(rx, ry, rw, std::max(60, r.y + r.h - 50 - ry)));

        // ---- centre: pixel grid
        Rect area(r.x + 196, r.y + 32, r.w - 196 - 316, r.h - 32 - 50);
        c.fill(area, 0x808080);
        zoom = std::clamp(std::min(area.w / std::max(1, w), area.h / std::max(1, h)), 2, 32);
        int gx = area.x + (area.w - w * zoom) / 2, gy = area.y + (area.h - h * zoom) / 2;
        Layer& L = cur();
        uint32_t fgRGB = 0, bgRGB = 0xFFFFFF;
        if (mode == IMAGE) fgRGB = rgbOfVdi(a, bitColor);
        else if (!isColourView()) { fgRGB = rgbOfVdi(a, (icon.ch >> 12) & 15); bgRGB = rgbOfVdi(a, (icon.ch >> 8) & 15); }
        // preview of shape tools
        Layer show = L;
        if (dragging && (tool == LINE || tool == RECT || tool == FRECT)) {
            Layer saved = cur();
            int v = valueFor(dragButton);
            if (tool == LINE) lineTo(sx, sy, ex, ey, v);
            else rect(sx, sy, ex, ey, v, tool == FRECT);
            show = cur();
            cur() = saved;
        }
        c.pushClip(area);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                size_t i = size_t(y) * w + x;
                uint32_t rgb;
                bool m = mode == IMAGE ? true : show.mask[i];
                int p = show.pix[i];
                if (target == 1 && mode != IMAGE) {
                    rgb = m ? 0x202020 : 0xF0F0F0;
                    if (p && !isColourView()) rgb = m ? 0x606060 : 0xC0C0C0;
                } else if (mode == IMAGE) {
                    rgb = p ? fgRGB : 0xFFFFFF;
                } else if (isColourView()) {
                    rgb = m ? rgbOfPen(a, p) : (((x + y) & 1) ? 0xD8D8D8 : 0xF8F8F8);
                    if (!m && p) rgb = rgbOfPen(a, p) & 0xC0C0C0;
                } else {
                    rgb = p ? fgRGB : m ? bgRGB : (((x + y) & 1) ? 0xD8D8D8 : 0xF8F8F8);
                }
                c.fill(Rect(gx + x * zoom, gy + y * zoom, zoom, zoom), rgb);
            }
        if (zoom >= 6) {
            for (int x = 0; x <= w; x++) c.vline(gx + x * zoom, gy, gy + h * zoom, (x % 8) ? 0xB0B0B0 : 0x606060);
            for (int y = 0; y <= h; y++) c.hline(gx, gx + w * zoom, gy + y * zoom, (y % 8) ? 0xB0B0B0 : 0x606060);
        }
        c.popClip();
        c.frame(Rect(gx - 1, gy - 1, w * zoom + 2, h * zoom + 2), col::line);

        // mouse on grid
        int px = (u.in.mx - gx) / std::max(1, zoom), py = (u.in.my - gy) / std::max(1, zoom);
        if (u.in.mx < gx) px = -1;
        if (u.in.my < gy) py = -1;
        bool inGrid = u.hover(Rect(gx, gy, w * zoom, h * zoom));
        if (inGrid) {
            a.cursor_ = Cursor::Crosshair;
            c.text(area.x + 4, area.y + area.h - 18, std::to_string(px) + "," + std::to_string(py), 0xFFFFFF);
        }
        for (int b : { 0, 1 }) {
            if (inGrid && u.in.pressed[b]) {
                snapshot();
                dragging = true;
                dragButton = b;
                sx = ex = px; sy = ey = py;
                if (tool == PENCIL) plot(px, py, valueFor(b));
                else if (tool == FILL) { flood(px, py, valueFor(b)); dragging = false; }
                else if (tool == PICK) {
                    int pen = L.pix[size_t(py) * w + px];
                    gem::Screen tmp;
                    tmp.planes = planes();
                    colour = tmp.vdiOfPen(pen);
                    tool = PENCIL;
                    dragging = false;
                    undoStack.pop_back();
                }
            }
        }
        if (dragging) {
            int cx = std::clamp(px, 0, w - 1), cy = std::clamp(py, 0, h - 1);
            if (tool == PENCIL && u.in.down[dragButton]) { lineTo(ex, ey, cx, cy, valueFor(dragButton)); }
            ex = cx; ey = cy;
            if (!u.in.down[dragButton]) {
                if (tool == LINE) lineTo(sx, sy, ex, ey, valueFor(dragButton));
                else if (tool == RECT || tool == FRECT) rect(sx, sy, ex, ey, valueFor(dragButton), tool == FRECT);
                dragging = false;
            }
        }
        for (auto it = u.in.keys.begin(); it != u.in.keys.end();) {
            if ((it->mods & MOD_CTRL) && it->sym == 'z') { restore(); it = u.in.keys.erase(it); }
            else ++it;
        }

        // ---- buttons
        bool ok = u.button(Rect(r.x + r.w - 220, r.y + r.h - 34, 100, 24), "OK");
        bool cancel = u.button(Rect(r.x + r.w - 110, r.y + r.h - 34, 100, 24), "Cancel");
        c.text(r.x + 196, r.y + r.h - 30, std::to_string(w) + " x " + std::to_string(h) + " pixels", col::dim);
        for (auto& k : u.in.keys) {
            if (k.sym == K_ESCAPE) cancel = true;
            if (k.sym == K_RETURN && u.focus == 0) ok = true;
        }
        if (ok) { apply(a); return false; }
        return !cancel;
    }
};

} // namespace

Modal* makeBitmapEditor(App& app)
{
    auto* e = new BitmapEditor;
    e->load(app);
    return e;
}

} // namespace app
