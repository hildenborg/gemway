#include "ui/ui.h"
#include "base/bytes.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ui {

using namespace platform;

Id Ui::hash(const std::string& s, Id seed)
{
    Id h = seed ? seed : 1469598103934665603ull;
    for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ull; }
    return h ? h : 1;
}

void Ui::beginFrame()
{
    block_.swap(nextBlock_);
    nextBlock_.clear();
    overlays_.clear();
    ids_.clear();
    focusSeen_ = false;
    // a click outside any open popup closes it and is swallowed
    popupDrawn_ = false;
    if (popup && (in.pressed[0] || in.pressed[1])) {
        bool inside = false;
        for (auto& r : block_) if (r.contains(in.mx, in.my)) inside = true;
        if (!inside) {
            popup = 0;
            consumeClick();
        }
    }
    if (!in.down[0] && !in.released[0]) active = 0;
}

void Ui::endFrame()
{
    bool saved = enabled;
    enabled = true;
    inOverlay_ = true;
    auto ovs = overlays_;
    overlays_.clear();
    for (auto& o : ovs) {
        nextBlock_.push_back(o.r);
        o.fn();
    }
    inOverlay_ = false;
    enabled = saved;
    if (popup && !popupDrawn_) popup = 0;
    // a focused field that was not drawn this frame loses focus
    if (focus && !focusSeen_) focus = 0;
}

bool Ui::blocked() const
{
    if (inOverlay_) return false;
    if (popup) return true;
    for (auto& r : block_) if (r.contains(in.mx, in.my)) return true;
    return false;
}

bool Ui::hover(const Rect& r) const
{
    return enabled && !blocked() && r.contains(in.mx, in.my) && c.clip().contains(in.mx, in.my);
}

// ---------------------------------------------------------------- basic widgets
static void bevel(Canvas& c, const Rect& r, uint32_t face, bool pressed)
{
    c.fill(r, face);
    c.frame(r, col::line);
    if (!pressed) {
        c.hline(r.x + 1, r.x + r.w - 2, r.y + 1, 0xFFFFFF);
        c.vline(r.x + 1, r.y + 1, r.y + r.h - 2, 0xFFFFFF);
        c.hline(r.x + 1, r.x + r.w - 2, r.y + r.h - 2, col::light);
        c.vline(r.x + r.w - 2, r.y + 1, r.y + r.h - 2, col::light);
    }
}

bool Ui::button(const Rect& r, const std::string& label, bool en, bool toggled)
{
    bool h = en && hover(r);
    bool press = h && in.down[0];
    uint32_t face = toggled ? col::selBg : press ? col::light : h ? col::hover : col::panel;
    bevel(c, r, face, press || toggled);
    c.textCentered(r, label, !en ? col::disabled : toggled ? col::selText : col::text);
    return h && in.pressed[0];
}

bool Ui::smallButton(const Rect& r, const std::string& label, bool en)
{
    return button(r, label, en, false);
}

bool Ui::checkbox(const Rect& r, const std::string& label, bool& v, bool en)
{
    Rect box(r.x, r.y + (r.h - 12) / 2, 12, 12);
    bool h = en && hover(r);
    c.fill(box, en ? col::field : col::panel);
    c.frame(box, en ? col::line : col::disabled);
    if (v) {
        uint32_t k = en ? col::text : col::disabled;
        c.line(box.x + 2, box.y + 6, box.x + 4, box.y + 9, k);
        c.line(box.x + 4, box.y + 9, box.x + 9, box.y + 2, k);
        c.line(box.x + 3, box.y + 6, box.x + 5, box.y + 9, k);
        c.line(box.x + 5, box.y + 9, box.x + 10, box.y + 2, k);
    }
    if (h) c.frame(box.inset(-1), col::accent2);
    c.text(r.x + 17, r.y + (r.h - 16) / 2, label, en ? col::text : col::disabled, r.w - 17);
    if (h && in.pressed[0]) { v = !v; return true; }
    return false;
}

bool Ui::radio(const Rect& r, const std::string& label, bool on)
{
    Rect box(r.x, r.y + (r.h - 12) / 2, 12, 12);
    bool h = hover(r);
    c.fill(box, col::field);
    c.frame(box, col::line);
    if (on) c.fill(box.inset(3), col::text);
    if (h) c.frame(box.inset(-1), col::accent2);
    c.text(r.x + 17, r.y + (r.h - 16) / 2, label, col::text, r.w - 17);
    return h && in.pressed[0];
}

void Ui::label(const Rect& r, const std::string& s, uint32_t color, int align)
{
    int y = r.y + (r.h - 16) / 2;
    if (align == 0) c.text(r.x, y, s, color, r.w);
    else if (align == 1) c.text(r.x + r.w - Canvas::textW(s), y, s, color);
    else c.textCentered(r, s, color);
}

void Ui::header(const Rect& r, const std::string& s)
{
    c.fill(r, col::selBg);
    c.text(r.x + 4, r.y + (r.h - 16) / 2, s, col::selText, r.w - 8);
}

// ---------------------------------------------------------------- text fields
bool Ui::textField(Id fid, const Rect& r, std::string& s, int maxLen, bool en)
{
    bool focused = focus == fid && en;
    if (focused) focusSeen_ = true;
    bool h = en && hover(r);
    c.fill(r, en ? col::field : col::panel);
    c.frame(r, focused ? col::accent2 : en ? col::line : col::disabled);
    if (focused) c.frame(r.inset(1), col::accent2);
    bool changed = false;

    if (h && in.pressed[0]) {
        if (!focused) { focus = fid; focused = true; focusSeen_ = true; }
        int rel = (in.mx - r.x - 4 + 4) / 8;
        cursor = std::clamp(rel, 0, (int)s.size());
    }
    if (focused) {
        if (cursor < 0 || cursor > (int)s.size()) cursor = (int)s.size();
        std::vector<KeyEvent> rest;
        for (auto& k : in.keys) {
            switch (k.sym) {
            case K_LEFT: cursor = std::max(0, cursor - 1); break;
            case K_RIGHT: cursor = std::min((int)s.size(), cursor + 1); break;
            case K_HOME: cursor = 0; break;
            case K_END: cursor = (int)s.size(); break;
            case K_BACKSPACE:
                if (cursor > 0) { s.erase(size_t(cursor - 1), 1); cursor--; changed = true; }
                break;
            case K_DELETE:
                if (cursor < (int)s.size()) { s.erase(size_t(cursor), 1); changed = true; }
                break;
            case K_RETURN: case K_KP_ENTER: case K_ESCAPE: case K_TAB: case K_ISO_LEFT_TAB:
                focus = 0;
                break;
            default:
                if (!k.text.empty() && !(k.mods & (MOD_CTRL | MOD_ALT))) {
                    std::string a = base::utf8ToAtari(k.text);
                    if ((int)(s.size() + a.size()) <= maxLen) {
                        s.insert(size_t(cursor), a);
                        cursor += (int)a.size();
                        changed = true;
                    }
                } else if ((k.mods & MOD_CTRL) && k.sym == 'u') {
                    s.clear(); cursor = 0; changed = true;
                } else {
                    rest.push_back(k);
                }
            }
            if (focus != fid) break;
        }
        in.keys.swap(rest);
        if (focus != fid) focused = false;
    }
    // visible window
    int cap = std::max(1, (r.w - 8) / 8);
    int start = 0;
    if (focused && cursor > cap - 1) start = cursor - cap + 1;
    c.pushClip(r.inset(2));
    c.text(r.x + 4, r.y + (r.h - 16) / 2, s.substr(std::min(s.size(), size_t(start))), en ? col::text : col::disabled);
    if (focused) {
        int cx = r.x + 4 + (cursor - start) * 8;
        c.fill(Rect(cx - 1, r.y + 3, 2, r.h - 6), col::accent2);
    }
    c.popClip();
    return changed;
}

bool Ui::intField(Id fid, const Rect& r, int& v, int lo, int hi, bool en)
{
    // [-] [value] [+]
    Rect minus(r.x, r.y, 16, r.h), plus(r.x + r.w - 16, r.y, 16, r.h), mid(r.x + 15, r.y, r.w - 30, r.h);
    bool changed = false;
    if (button(minus, "-", en && v > lo)) { v = std::max(lo, v - 1); changed = true; }
    if (button(plus, "+", en && v < hi)) { v = std::min(hi, v + 1); changed = true; }
    std::string s = std::to_string(v);
    // keep partial input (e.g. "-") while typing
    static std::string partial;
    static Id partialId = 0;
    if (focus == fid && partialId == fid) s = partial;
    if (textField(fid, mid, s, 8, en)) {
        partial = s;
        partialId = fid;
        char* end = nullptr;
        long n = strtol(s.c_str(), &end, 10);
        if (end != s.c_str() && *end == 0) {
            n = std::clamp<long>(n, lo, hi);
            if (n != v) { v = int(n); changed = true; }
        }
    }
    if (focus != fid && partialId == fid) partialId = 0;
    if (hover(mid) && in.wheelY != 0 && en) {
        int d = in.wheelY < 0 ? 1 : -1;
        int nv = std::clamp(v + d, lo, hi);
        if (nv != v) { v = nv; changed = true; }
        in.wheelY = 0;
    }
    return changed;
}

bool Ui::hexField(Id fid, const Rect& r, uint32_t& v, int digits, bool en)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%0*X", digits, v);
    std::string s = buf;
    static std::string partial;
    static Id partialId = 0;
    if (focus == fid && partialId == fid) s = partial;
    bool changed = false;
    if (textField(fid, r, s, digits, en)) {
        partial = s;
        partialId = fid;
        char* end = nullptr;
        unsigned long n = strtoul(s.c_str(), &end, 16);
        if (!s.empty() && *end == 0 && n != v) { v = uint32_t(n); changed = true; }
    }
    if (focus != fid && partialId == fid) partialId = 0;
    return changed;
}

// ---------------------------------------------------------------- popups
void Ui::popupList(Id pid, const Rect& anchor, const std::vector<std::string>& items, const std::vector<bool>& en,
                   int sel, const std::function<void(int, const Rect&)>& drawItem)
{
    int iw = anchor.w;
    for (auto& s : items) iw = std::max(iw, Canvas::textW(s) + 24);
    int ih = 18;
    int n = (int)items.size();
    int maxVisible = std::max(4, (c.h() - 20) / ih);
    int vis = std::min(n, maxVisible);
    Rect r(anchor.x, anchor.y + anchor.h, iw, vis * ih + 4);
    if (r.y + r.h > c.h()) r.y = std::max(0, anchor.y - r.h);
    if (r.x + r.w > c.w()) r.x = std::max(0, c.w() - r.w);
    popupDrawn_ = true;
    int first = 0;
    if (sel >= vis) first = std::min(sel - vis / 2, n - vis);
    overlays_.push_back({ r, [=, this]() {
        c.pushClip(Rect(0, 0, c.w(), c.h()));
        c.fill(Rect(r.x + 3, r.y + 3, r.w, r.h), 0x606060);
        c.fill(r, col::field);
        c.frame(r, col::line);
        static int scrollFirst = 0;
        static Id scrollFor = 0;
        if (scrollFor != pid) { scrollFor = pid; scrollFirst = first; }
        if (r.contains(in.mx, in.my) && in.wheelY != 0) {
            scrollFirst = std::clamp(scrollFirst + (in.wheelY > 0 ? 3 : -3), 0, std::max(0, n - vis));
            in.wheelY = 0;
        }
        for (int k = 0; k < vis; k++) {
            int i = scrollFirst + k;
            if (i >= n) break;
            Rect ir(r.x + 2, r.y + 2 + k * ih, r.w - 4, ih);
            bool ok = en.empty() || en[size_t(i)];
            bool sep = items[size_t(i)] == "-";
            bool h = ok && !sep && ir.contains(in.mx, in.my);
            if (sep) {
                c.hline(ir.x + 2, ir.x + ir.w - 3, ir.y + ih / 2, col::light);
                continue;
            }
            if (h || i == sel) c.fill(ir, h ? col::selBg : col::hover);
            if (drawItem) drawItem(i, Rect(ir.x + 4, ir.y + 1, ir.w - 8, ih - 2));
            c.text(ir.x + (drawItem ? 28 : 6), ir.y + 1, items[size_t(i)], h ? col::selText : ok ? col::text : col::disabled,
                   ir.w - 8);
            if (h && in.released[0] && popup == pid) {
                resultId_ = pid;
                resultVal_ = i;
                popup = 0;
            }
        }
        c.popClip();
    } });
}

bool Ui::dropdown(Id did, const Rect& r, int& sel, const std::vector<std::string>& items, bool en,
                  const std::function<void(int, const Rect&)>& drawItem)
{
    bool changed = false;
    if (resultId_ == did) {
        if (resultVal_ != sel) { sel = resultVal_; changed = true; }
        resultId_ = 0;
    }
    bool h = en && hover(r);
    bevel(c, r, h ? col::hover : col::field, false);
    std::string cur = (sel >= 0 && sel < (int)items.size()) ? items[size_t(sel)] : "";
    int tx = r.x + 5;
    if (drawItem && sel >= 0) { drawItem(sel, Rect(r.x + 4, r.y + 2, r.w - 24, r.h - 4)); tx = r.x + 28; }
    c.pushClip(Rect(r.x, r.y, r.w - 16, r.h));
    c.text(tx, r.y + (r.h - 16) / 2, cur, en ? col::text : col::disabled);
    c.popClip();
    // arrow
    int ax = r.x + r.w - 12, ay = r.y + r.h / 2 - 2;
    for (int i = 0; i < 4; i++) c.hline(ax + i, ax + 6 - i, ay + i, en ? col::text : col::disabled);
    if (h && in.pressed[0]) {
        popup = popup == did ? 0 : did;
        popupAnchor = r;
        consumeClick();
    }
    if (popup == did) popupList(did, r, items, {}, sel, drawItem);
    return changed;
}

void Ui::openMenu(Id mid, int x, int y, const std::vector<std::string>& items, const std::vector<bool>& en)
{
    popup = mid;
    popupAnchor = Rect(x, y, 0, 0);
    menuItems_ = items;
    menuEnabled_ = en;
}

int Ui::menuResult(Id mid)
{
    if (popup == mid) popupList(mid, popupAnchor, menuItems_, menuEnabled_, -1, nullptr);
    if (resultId_ == mid) {
        resultId_ = 0;
        return resultVal_;
    }
    return -1;
}

void Ui::scrollbar(Id sid, const Rect& r, int content, int view, int& offset)
{
    int maxOff = std::max(0, content - view);
    offset = std::clamp(offset, 0, maxOff);
    c.fill(r, col::panel);
    c.frame(r, col::light);
    if (maxOff <= 0) return;
    int th = std::max(16, r.h * view / std::max(1, content));
    int ty = r.y + (r.h - th) * offset / maxOff;
    Rect thumb(r.x + 1, ty, r.w - 2, th);
    bool h = hover(thumb);
    c.fill(thumb, active == sid || h ? col::dim : col::light);
    c.frame(thumb, col::line);
    if (h && in.pressed[0]) { active = sid; dragOff_ = in.my - ty; }
    else if (hover(r) && in.pressed[0]) {
        offset = std::clamp(offset + (in.my < ty ? -view : view), 0, maxOff);
    }
    if (active == sid && in.down[0]) {
        int ny = in.my - dragOff_;
        offset = std::clamp((ny - r.y) * maxOff / std::max(1, r.h - th), 0, maxOff);
    }
}

} // namespace ui
