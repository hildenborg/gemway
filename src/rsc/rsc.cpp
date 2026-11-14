#include "rsc/rsc.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <set>

namespace rsc {

// ------------------------------------------------------------ type info
const char* typeName(int t)
{
    switch (t) {
    case G_BOX: return "G_BOX"; case G_TEXT: return "G_TEXT"; case G_BOXTEXT: return "G_BOXTEXT";
    case G_IMAGE: return "G_IMAGE"; case G_USERDEF: return "G_USERDEF"; case G_IBOX: return "G_IBOX";
    case G_BUTTON: return "G_BUTTON"; case G_BOXCHAR: return "G_BOXCHAR"; case G_STRING: return "G_STRING";
    case G_FTEXT: return "G_FTEXT"; case G_FBOXTEXT: return "G_FBOXTEXT"; case G_ICON: return "G_ICON";
    case G_TITLE: return "G_TITLE"; case G_CICON: return "G_CICON"; case G_SWBUTTON: return "G_SWBUTTON";
    case G_POPUP: return "G_POPUP"; case G_WINTITLE: return "G_WINTITLE"; case G_EDIT: return "G_EDIT";
    case G_SHORTCUT: return "G_SHORTCUT";
    }
    return "G_?";
}

const char* shortTypeName(int t) { const char* n = typeName(t); return n[1] == '_' ? n + 2 : n; }

bool typeHasTedinfo(int t) { return t == G_TEXT || t == G_BOXTEXT || t == G_FTEXT || t == G_FBOXTEXT; }
bool typeHasString(int t) { return t == G_STRING || t == G_BUTTON || t == G_TITLE || t == G_SHORTCUT || t == G_SWBUTTON || t == G_POPUP; }
bool typeHasBoxSpec(int t) { return t == G_BOX || t == G_IBOX || t == G_BOXCHAR; }

// ------------------------------------------------------------ bitmaps
void Bitmap::resize(int nw, int nh)
{
    nw = (nw + 15) & ~15;
    std::vector<uint16_t> n(size_t(nw / 16) * nh, 0);
    for (int y = 0; y < std::min(h, nh); y++)
        for (int x = 0; x < std::min(w, nw); x++)
            if (get(x, y)) n[size_t(y) * (nw / 16) + x / 16] |= uint16_t(0x8000 >> (x & 15));
    w = nw; h = nh; words.swap(n);
}

bool Bitmap::get(int x, int y) const
{
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    size_t i = size_t(y) * wpr() + x / 16;
    return i < words.size() && (words[i] & (0x8000 >> (x & 15)));
}

void Bitmap::set(int x, int y, bool v)
{
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    size_t i = size_t(y) * wpr() + x / 16;
    if (i >= words.size()) return;
    uint16_t m = uint16_t(0x8000 >> (x & 15));
    if (v) words[i] |= m; else words[i] &= uint16_t(~m);
}

int CIcon::pixel(const std::vector<uint16_t>& d, int wpr, int h, int x, int y) const
{
    size_t plane = size_t(wpr) * h;
    int v = 0;
    for (int p = 0; p < planes; p++) {
        size_t i = p * plane + size_t(y) * wpr + x / 16;
        if (i < d.size() && (d[i] & (0x8000 >> (x & 15)))) v |= 1 << p;
    }
    return v;
}

void CIcon::setPixel(std::vector<uint16_t>& d, int wpr, int h, int x, int y, int v) const
{
    size_t plane = size_t(wpr) * h;
    uint16_t m = uint16_t(0x8000 >> (x & 15));
    for (int p = 0; p < planes; p++) {
        size_t i = p * plane + size_t(y) * wpr + x / 16;
        if (i >= d.size()) continue;
        if (v & (1 << p)) d[i] |= m; else d[i] &= uint16_t(~m);
    }
}

bool Rsc::hasColourIcons() const
{
    for (auto& t : trees)
        for (auto& o : t.obj)
            if (o.baseType() == G_CICON) return true;
    return false;
}

// ------------------------------------------------------------ coordinates
int toPixels(int16_t raw, int cell, bool isWidth, int screenWidth)
{
    int c = rawChars(raw);
    int px = (isWidth && c == 80 && screenWidth > 0) ? screenWidth : c * cell;
    return px + rawPixOff(raw);
}

int16_t fromPixels(int px, int cell)
{
    if (px < 0) {
        // negative positions: 0 chars plus a negative pixel offset (>= -127)
        return makeRaw(0, std::max(px, -127));
    }
    int c = px / cell, o = px % cell;
    if (c > 255) { c = 255; o = std::min(px - 255 * cell, 127); }
    return makeRaw(c, o);
}

// ------------------------------------------------------------ tree ops
int parentOf(const Tree& t, int obj)
{
    if (obj <= 0 || obj >= (int)t.obj.size()) return -1;
    int cur = obj;
    int guard = 0;
    while (guard++ < 10000) {
        int nx = t.obj[cur].next;
        if (nx < 0 || nx >= (int)t.obj.size()) return -1;
        if (t.obj[nx].tail == cur) return nx;
        cur = nx;
    }
    return -1;
}

std::vector<int> childrenOf(const Tree& t, int obj)
{
    std::vector<int> v;
    if (obj < 0 || obj >= (int)t.obj.size()) return v;
    int c = t.obj[obj].head;
    int guard = 0;
    while (c >= 0 && c != obj && c < (int)t.obj.size() && guard++ < 10000) {
        v.push_back(c);
        c = t.obj[c].next;
    }
    return v;
}

bool isAncestor(const Tree& t, int anc, int obj)
{
    for (int p = parentOf(t, obj); p >= 0; p = parentOf(t, p))
        if (p == anc) return true;
    return false;
}

int depthOf(const Tree& t, int obj)
{
    int d = 0;
    for (int p = parentOf(t, obj); p >= 0; p = parentOf(t, p)) d++;
    return d;
}

void fixLastOb(Tree& t)
{
    for (auto& o : t.obj) o.flags &= ~LASTOB;
    if (!t.obj.empty()) t.obj.back().flags |= LASTOB;
}

int addObject(Tree& t, int parent, const Object& src)
{
    Object o = src;
    int idx = (int)t.obj.size();
    o.head = o.tail = -1;
    if (t.obj.empty() || parent < 0) {
        o.next = -1;
        t.obj.push_back(o);
        fixLastOb(t);
        return idx;
    }
    o.next = int16_t(parent);
    t.obj.push_back(o);
    Object& p = t.obj[parent];
    if (p.tail < 0) {
        p.head = p.tail = int16_t(idx);
    } else {
        t.obj[p.tail].next = int16_t(idx);
        p.tail = int16_t(idx);
    }
    fixLastOb(t);
    return idx;
}

int copySubtree(Tree& dst, int parent, const Tree& src, int srcRoot)
{
    int nr = addObject(dst, parent, src.obj[srcRoot]);
    for (int c : childrenOf(src, srcRoot)) copySubtree(dst, nr, src, c);
    return nr;
}

// unlink obj from its parent's child list (does not remove from array)
static void unlink(Tree& t, int obj)
{
    int par = parentOf(t, obj);
    if (par < 0) return;
    Object& p = t.obj[par];
    if (p.head == obj) {
        if (p.tail == obj) { p.head = p.tail = -1; }
        else p.head = t.obj[obj].next;
    } else {
        int c = p.head;
        while (c >= 0 && t.obj[c].next != obj) c = t.obj[c].next;
        if (c >= 0) {
            t.obj[c].next = t.obj[obj].next;
            if (p.tail == obj) p.tail = int16_t(c);
        }
    }
    t.obj[obj].next = -1;
}

static void linkLast(Tree& t, int parent, int obj)
{
    Object& p = t.obj[parent];
    t.obj[obj].next = int16_t(parent);
    if (p.tail < 0) p.head = p.tail = int16_t(obj);
    else { t.obj[p.tail].next = int16_t(obj); p.tail = int16_t(obj); }
}

void deleteObject(Tree& t, int obj)
{
    if (obj < 0 || obj >= (int)t.obj.size()) return;
    if (obj == 0) { t.obj.clear(); return; }
    std::vector<bool> del(t.obj.size(), false);
    std::function<void(int)> mark = [&](int o) {
        del[o] = true;
        for (int c : childrenOf(t, o)) mark(c);
    };
    mark(obj);
    unlink(t, obj);
    std::vector<int> remap(t.obj.size(), -1);
    std::vector<Object> n;
    for (size_t i = 0; i < t.obj.size(); i++)
        if (!del[i]) { remap[i] = (int)n.size(); n.push_back(t.obj[i]); }
    auto rm = [&](int16_t v) -> int16_t { return v < 0 ? -1 : int16_t(remap[v]); };
    for (auto& o : n) { o.next = rm(o.next); o.head = rm(o.head); o.tail = rm(o.tail); }
    t.obj.swap(n);
    fixLastOb(t);
}

void reorderObject(Tree& t, int obj, int pos)
{
    int par = parentOf(t, obj);
    if (par < 0) return;
    std::vector<int> kids = childrenOf(t, par);
    kids.erase(std::remove(kids.begin(), kids.end(), obj), kids.end());
    pos = std::clamp(pos, 0, (int)kids.size());
    kids.insert(kids.begin() + pos, obj);
    Object& p = t.obj[par];
    p.head = int16_t(kids.front());
    p.tail = int16_t(kids.back());
    for (size_t i = 0; i < kids.size(); i++)
        t.obj[kids[i]].next = int16_t(i + 1 < kids.size() ? kids[i + 1] : par);
}

void reparentObject(Tree& t, int obj, int newParent)
{
    if (obj <= 0 || newParent < 0 || newParent == obj || isAncestor(t, obj, newParent)) return;
    unlink(t, obj);
    linkLast(t, newParent, obj);
}

void sortTree(Tree& t)
{
    if (t.obj.empty()) return;
    std::vector<int> order;
    std::function<void(int)> walk = [&](int o) {
        order.push_back(o);
        // pre-order: parent first, then each child's subtree in order
        std::vector<int> kids = childrenOf(t, o);
        for (int c : kids) walk(c);
    };
    walk(0);
    std::vector<int> remap(t.obj.size(), -1);
    for (size_t i = 0; i < order.size(); i++) remap[order[i]] = (int)i;
    std::vector<Object> n;
    for (int o : order) n.push_back(t.obj[o]);
    auto rm = [&](int16_t v) -> int16_t { return v < 0 ? -1 : int16_t(remap[v]); };
    for (auto& o : n) { o.next = rm(o.next); o.head = rm(o.head); o.tail = rm(o.tail); }
    t.obj.swap(n);
    fixLastOb(t);
}

std::string validateTree(const Tree& t)
{
    int n = (int)t.obj.size();
    if (n == 0) return "tree is empty";
    if (t.obj[0].next != -1) return "root ob_next is not -1";
    std::vector<int> seen(n, 0);
    std::function<bool(int, int)> walk = [&](int o, int depth) -> bool {
        if (depth > 64 || seen[o]++) return false;
        for (int c : childrenOf(t, o)) if (c >= n || !walk(c, depth + 1)) return false;
        return true;
    };
    if (!walk(0, 0)) return "object links form a cycle or are out of range";
    return {};
}

// ------------------------------------------------------------ factories
Object makeObject(int type, int cellW, int cellH)
{
    (void)cellH;
    Object o;
    o.type = uint16_t(type);
    o.w = makeRaw(8, 0);
    o.h = makeRaw(1, 0);
    switch (type) {
    case G_BOX:     o.spec = 0x00FF1100; o.w = makeRaw(10, 0); o.h = makeRaw(3, 0); break;
    case G_IBOX:    o.spec = 0x00011100; o.w = makeRaw(10, 0); o.h = makeRaw(3, 0); break;
    case G_BOXCHAR: o.spec = 0x41FF1100; o.w = makeRaw(2, 0); break;
    case G_STRING:  o.str = "String"; o.w = makeRaw(6, 0); break;
    case G_TITLE:   o.str = " Title "; o.w = makeRaw(7, 0); break;
    case G_BUTTON:  o.str = "Button"; o.flags = SELECTABLE | EXIT; break;
    case G_TEXT:
        o.ted.text = "Text"; o.ted.color = 0x1180; o.ted.just = TE_LEFT; o.ted.thickness = 0; o.w = makeRaw(4, 0);
        break;
    case G_BOXTEXT:
        o.ted.text = "Boxtext"; o.ted.color = 0x1180; o.ted.just = TE_CNTR; o.ted.thickness = -1; o.w = makeRaw(10, 0);
        break;
    case G_FTEXT:
        o.ted.text = ""; o.ted.tmplt = "Name: ________"; o.ted.valid = "XXXXXXXX";
        o.ted.color = 0x1180; o.ted.thickness = 0; o.flags = EDITABLE; o.w = makeRaw(14, 0);
        break;
    case G_FBOXTEXT:
        o.ted.text = ""; o.ted.tmplt = "________"; o.ted.valid = "XXXXXXXX";
        o.ted.color = 0x1180; o.ted.just = TE_CNTR; o.ted.thickness = -1; o.flags = EDITABLE; o.w = makeRaw(10, 0);
        break;
    case G_IMAGE:
        o.bit.img.resize(32, 32); o.bit.color = 1; o.w = makeRaw(0, 32); o.h = makeRaw(0, 32);
        // default image: a frame
        for (int i = 0; i < 32; i++) { o.bit.img.set(i, 0, 1); o.bit.img.set(i, 31, 1); o.bit.img.set(0, i, 1); o.bit.img.set(31, i, 1); }
        o.w = makeRaw(4, 0); o.h = fromPixels(32, cellH > 0 ? cellH : 16);
        break;
    case G_ICON:
    case G_CICON: {
        IconBlk& ib = o.icon;
        ib.mask.resize(32, 32); ib.data.resize(32, 32);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                bool border = x == 0 || y == 0 || x == 31 || y == 31;
                ib.mask.set(x, y, 1);
                ib.data.set(x, y, border || (x == y) || (x == 31 - y));
            }
        ib.text = "ICON"; ib.ch = 0x1000;
        ib.xicon = 20; ib.yicon = 0; ib.wicon = 32; ib.hicon = 32;
        ib.xtext = 0; ib.ytext = 32; ib.wtext = 72; ib.htext = 8;
        o.w = makeRaw(9, 0); o.h = fromPixels(40, cellH > 0 ? cellH : 16);
        if (type == G_CICON) {
            CIcon ci; ci.planes = 4;
            int wpr = 2, h = 32;
            ci.col.assign(size_t(wpr) * h * 4, 0);
            ci.colMask = ib.mask.words;
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++)
                    ci.setPixel(ci.col, wpr, h, x, y, ib.data.get(x, y) ? 15 : ((x / 8 + y / 8) % 2 ? 4 : 0));
            o.cicons.push_back(ci);
        }
        break;
    }
    case G_USERDEF: o.spec = 0; o.w = makeRaw(8, 0); o.h = makeRaw(2, 0); break;
    default: o.str = "Ext"; break;
    }
    return o;
}

Tree makeDialogTree(const std::string& name, int cellW, int cellH)
{
    Tree t; t.name = name; t.kind = TreeKind::Dialog;
    Object root = makeObject(G_BOX, cellW, cellH);
    root.spec = 0x00021100; root.state = OUTLINED;
    root.w = makeRaw(40, 0); root.h = makeRaw(12, 0);
    addObject(t, -1, root);
    return t;
}

Tree makeMenuTree(const std::string& name)
{
    // Mirrors the layout produced by the Atari RCS for a new menu.
    Tree t; t.name = name; t.kind = TreeKind::Menu;
    Object root; root.type = G_IBOX; root.spec = 0; root.w = makeRaw(80, 0); root.h = makeRaw(25, 0);
    addObject(t, -1, root);
    Object bar; bar.type = G_BOX; bar.spec = 0x00001100; bar.w = makeRaw(80, 0); bar.h = makeRaw(1, 2);
    int b = addObject(t, 0, bar);
    Object act; act.type = G_IBOX; act.spec = 0; act.x = makeRaw(2, 0); act.w = makeRaw(12, 0); act.h = makeRaw(1, 3);
    addObject(t, b, act);
    Object scr; scr.type = G_IBOX; scr.spec = 0; scr.y = makeRaw(1, 3); scr.w = makeRaw(80, 0); scr.h = makeRaw(19, 0);
    addObject(t, 0, scr);

    int title = menuAddTitle(t, " Desk ");
    (void)title;
    MenuInfo mi; analyseMenu(t, mi);
    int box = mi.boxes[0];
    menuAddItem(t, box, "  About this program...", false);
    menuAddItem(t, box, "", true);
    for (int i = 1; i <= 6; i++) menuAddItem(t, box, "  Desk Accessory " + std::to_string(i), false);
    menuAddTitle(t, " File ");
    analyseMenu(t, mi);
    menuAddItem(t, mi.boxes[1], "  Quit", false);
    menuRelayout(t, 8, 16);
    sortTree(t);
    return t;
}

bool analyseMenu(const Tree& t, MenuInfo& mi)
{
    mi = MenuInfo();
    if (t.obj.size() < 4) return false;
    std::vector<int> rk = childrenOf(t, 0);
    if (rk.size() < 2) return false;
    mi.bar = rk[0];
    mi.screen = rk[1];
    std::vector<int> bk = childrenOf(t, mi.bar);
    if (bk.empty()) return false;
    mi.activeBox = bk[0];
    mi.titles = childrenOf(t, mi.activeBox);
    mi.boxes = childrenOf(t, mi.screen);
    for (int ti : mi.titles) if (t.obj[ti].baseType() != G_TITLE) return false;
    return !mi.titles.empty();
}

int menuAddTitle(Tree& t, const std::string& text)
{
    MenuInfo mi;
    if (!analyseMenu(t, mi) && mi.activeBox < 0) return -1;
    Object ti; ti.type = G_TITLE; ti.str = text; ti.w = makeRaw((int)text.size(), 0); ti.h = makeRaw(1, 3);
    int idx = addObject(t, mi.activeBox, ti);
    Object box; box.type = G_BOX; box.spec = 0x00FF1100; box.w = makeRaw(20, 0); box.h = makeRaw(0, 0);
    addObject(t, mi.screen, box);
    return idx;
}

int menuAddItem(Tree& t, int box, const std::string& text, bool separator)
{
    Object it; it.type = G_STRING;
    int w = rawChars(t.obj[box].w);
    if (separator) { it.str = std::string(std::max(w, 1), '-'); it.state = DISABLED; }
    else it.str = text;
    it.w = makeRaw(std::max(w, (int)it.str.size()), 0);
    it.h = makeRaw(1, 0);
    int n = (int)childrenOf(t, box).size();
    it.y = makeRaw(n, 0);
    int idx = addObject(t, box, it);
    t.obj[box].h = makeRaw(n + 1, 0);
    return idx;
}

void menuRelayout(Tree& t, int cellW, int cellH)
{
    (void)cellW; (void)cellH;
    MenuInfo mi;
    if (!analyseMenu(t, mi)) return;
    // titles side by side, active box spans them
    int x = 0;
    std::vector<int> titleX;
    for (int ti : mi.titles) {
        Object& o = t.obj[ti];
        o.x = makeRaw(x, 0);
        o.y = 0;
        int w = std::max(1, (int)o.str.size());
        o.w = makeRaw(w, 0);
        titleX.push_back(x);
        x += w;
    }
    Object& act = t.obj[mi.activeBox];
    act.w = makeRaw(x, 0);
    int ax = rawChars(act.x);
    // dropdown boxes below their titles
    for (size_t i = 0; i < mi.boxes.size(); i++) {
        Object& b = t.obj[mi.boxes[i]];
        int tx = ax + (i < titleX.size() ? titleX[i] : 0);
        b.x = makeRaw(i == 0 ? tx : tx, 0);
        b.y = 0;
        std::vector<int> items = childrenOf(t, mi.boxes[i]);
        int wmax = 1;
        for (int it : items)
            if (!(t.obj[it].state & DISABLED && !t.obj[it].str.empty() && t.obj[it].str.find_first_not_of('-') == std::string::npos))
                wmax = std::max(wmax, (int)t.obj[it].str.size());
        int row = 0;
        for (int it : items) {
            Object& o = t.obj[it];
            o.x = 0; o.y = makeRaw(row++, 0);
            o.w = makeRaw(wmax, 0); o.h = makeRaw(1, 0);
            bool sep = !o.str.empty() && o.str.find_first_not_of('-') == std::string::npos;
            if (sep) o.str = std::string(wmax, '-');
        }
        b.w = makeRaw(wmax, 0);
        b.h = makeRaw(row, 0);
    }
}

// ------------------------------------------------------------ names
std::string makeIdentifier(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '_') o += char(toupper((unsigned char)c));
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    if (o.empty()) return o;
    if (isdigit((unsigned char)o[0])) o = "N" + o;
    return o;
}

void ensureTreeNames(Rsc& r)
{
    char buf[32];
    for (size_t i = 0; i < r.trees.size(); i++)
        if (r.trees[i].name.empty()) {
            snprintf(buf, sizeof buf, r.trees[i].kind == TreeKind::Menu ? "MENU%02zu" : "TREE%02zu", i + 1);
            r.trees[i].name = buf;
        }
    for (size_t i = 0; i < r.strings.size(); i++)
        if (r.strings[i].name.empty()) {
            snprintf(buf, sizeof buf, r.strings[i].isAlert() ? "ALERT%02zu" : "STRING%02zu", i + 1);
            r.strings[i].name = buf;
        }
    for (size_t i = 0; i < r.images.size(); i++)
        if (r.images[i].name.empty()) { snprintf(buf, sizeof buf, "IMAGE%02zu", i + 1); r.images[i].name = buf; }
}

std::vector<std::string> checkNames(const Rsc& r)
{
    std::vector<std::string> problems;
    std::set<std::string> seen;
    auto chk = [&](const std::string& n, const std::string& where) {
        if (n.empty()) return;
        if (makeIdentifier(n) != n && n != makeIdentifier(n)) {
            bool ok = !isdigit((unsigned char)n[0]);
            for (char c : n) if (!(isalnum((unsigned char)c) || c == '_')) ok = false;
            if (!ok) problems.push_back("invalid identifier '" + n + "' (" + where + ")");
        }
        if (!seen.insert(n).second) problems.push_back("duplicate name '" + n + "' (" + where + ")");
    };
    for (auto& t : r.trees) {
        chk(t.name, "tree");
        for (size_t i = 0; i < t.obj.size(); i++) chk(t.obj[i].name, "object " + std::to_string(i) + " in " + t.name);
    }
    for (auto& s : r.strings) chk(s.name, "free string");
    for (auto& s : r.images) chk(s.name, "free image");
    return problems;
}

} // namespace rsc
