#include "gem/aes.h"
#include <algorithm>
#include <functional>

namespace gem {

using namespace rsc;

namespace {
constexpr int IP_HOLLOW = 0, IP_4PATT = 4, IP_SOLID = 7;
constexpr int FL3DOBJ = 0x0200;
constexpr int ADJ3DSTD = 2;

void crack(uint16_t color, int& bc, int& tc, int& ip, int& ic, int& md)
{
    bc = (color >> 12) & 15;
    tc = (color >> 8) & 15;
    md = (color & 0x80) ? MD_REPLACE : MD_TRANS;
    ip = (color >> 4) & 7;
    ic = color & 15;
}
} // namespace

// ---------------------------------------------------------------- geometry
void Aes::layout(const Tree& t, int ox, int oy, Layout& L) const
{
    size_t n = t.obj.size();
    L.rel.assign(n, Rect());
    L.abs.assign(n, Rect());
    int cw = env_.res.cellW, ch = env_.res.cellH, sw = env_.res.w;
    for (size_t i = 0; i < n; i++) {
        const Object& o = t.obj[i];
        L.rel[i] = Rect(toPixels(o.x, cw), toPixels(o.y, ch), toPixels(o.w, cw, true, sw), toPixels(o.h, ch));
    }
    if (n == 0) return;
    L.abs[0] = Rect(ox, oy, L.rel[0].w, L.rel[0].h);
    std::function<void(int, int)> walk = [&](int o, int depth) {
        if (depth > MAX_DEPTH) return;
        for (int c : childrenOf(t, o)) {
            L.abs[c] = Rect(L.abs[o].x + L.rel[c].x, L.abs[o].y + L.rel[c].y, L.rel[c].w, L.rel[c].h);
            walk(c, depth + 1);
        }
    };
    walk(0, 0);
}

void Aes::center(int w, int h, int& x, int& y) const
{
    int hb = env_.hbox();
    x = ((env_.res.w - w) / 2) & ~7;
    y = hb + (env_.res.h - hb - h) / 2;
    if (x < 0) x = 0;
    if (y < hb) y = hb;
}

Rect Aes::extent(const Tree& t, const Layout& L, int idx) const
{
    const Object& o = t.obj[idx];
    Rect r = L.abs[idx];
    int th = 0;
    switch (o.baseType()) {
    case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT: th = o.ted.thickness; break;
    case G_BOX: case G_IBOX: case G_BOXCHAR: th = int8_t((o.spec >> 16) & 0xFF); break;
    case G_BUTTON: th = -1 - ((o.flags & EXIT) ? 1 : 0) - ((o.flags & DEFAULT) ? 1 : 0); break;
    }
    int g = 0;
    if (env_.use3D() && (o.flags & FL3DOBJ)) g = ADJ3DSTD;
    if (th < 0) g = std::max(g, -th);
    Rect e = r.inset(-g);
    if (o.state & OUTLINED) e = e.unite(r.inset(-3 - (env_.use3D() && (o.flags & FL3DOBJ) ? ADJ3DSTD : 0)));
    if (o.state & SHADOWED) {
        int a = std::abs(th);
        Rect s = r.inset(th < 0 ? th : 0);
        e = e.unite(Rect(s.x, s.y, s.w + 2 * a, s.h + 2 * a));
    }
    return e;
}

// ---------------------------------------------------------------- primitives
void Aes::grBox(int x, int y, int w, int h, int th, int color)
{
    if (th == 0) return;
    if (th < 0) th--;
    do {
        th += (th > 0) ? -1 : 1;
        Rect n = Rect(x, y, w, h).inset(th);
        s_.box(n, MD_REPLACE, color);
    } while (th != 0);
}

void Aes::grRect(int icol, int ipat, const Rect& r)
{
    int fis = FIS_PATTERN;
    if (ipat == IP_HOLLOW) fis = FIS_HOLLOW;
    else if (ipat == IP_SOLID) fis = FIS_SOLID;
    s_.fill(r.x, r.y, r.w, r.h, MD_REPLACE, fis, ipat, icol);
}

// gr_gtext(): justify within rect, clip character count to the rect
void Aes::grText(int just, int fnt, const std::string& text, const Rect& r, int mode, int color)
{
    const SysFont& f = font(fnt);
    int n = (int)text.size();
    int tw = std::min(r.w, n * f.cw);
    int thh = std::min(r.h, f.ch);
    int numchs = (thh / f.ch) ? std::min(n, f.cw ? tw / f.cw : 0) : 0;
    if (tw < 0) numchs = 0;
    int x = r.x, y = r.y;
    int h = r.h - thh;
    if (h > 0) y += (h + 1) / 2;
    int w = r.w - tw;
    if (w > 0) {
        if (just == TE_CNTR) x += (w + 1) / 2;
        else if (just == TE_RIGHT) x += w;
    }
    if (numchs > 0) s_.text(f, x, y, text.data(), numchs, mode, color);
}

std::string Aes::formatText(int just, const std::string& rawIn, const std::string& tmpl)
{
    std::string raw = rawIn;
    if (!raw.empty() && raw[0] == '@') raw.clear();
    std::string out(tmpl.size(), ' ');
    int tl = (int)tmpl.size(), rl = (int)raw.size();
    if (just == TE_RIGHT) {
        int ri = rl - 1;
        for (int i = tl - 1; i >= 0; i--) {
            if (tmpl[i] != '_') out[i] = tmpl[i];
            else out[i] = ri >= 0 ? raw[ri--] : '_';
        }
    } else {
        int ri = 0;
        for (int i = 0; i < tl; i++) {
            if (tmpl[i] != '_') out[i] = tmpl[i];
            else out[i] = ri < rl ? raw[ri++] : '_';
        }
    }
    return out;
}

int Aes::xorColor(int c) const
{
    static const uint8_t comp4[] = { BLACK, WHITE, GREEN, RED };
    static const uint8_t comp16[] = { BLACK, WHITE, LCYAN, LMAGENTA, LYELLOW, LRED, LBLUE, LGREEN,
                                      LBLACK, LWHITE, CYAN, MAGENTA, YELLOW, RED, BLUE, GREEN };
    int nc = env_.ncolors();
    if (c >= 16 || c >= nc) return WHITE;
    if (nc <= 4) return comp4[c];
    return comp16[c];
}

bool Aes::xorIsOk(int type, int flags, const Object& o) const
{
    if (type == G_IBOX || type == G_STRING || type == G_TITLE) return true;
    if (type == G_IMAGE || (flags & FL3DOBJ)) return false;
    if (env_.ncolors() <= 16) return true;
    uint16_t cw;
    switch (type) {
    case G_BOXTEXT: case G_FBOXTEXT: case G_TEXT: case G_FTEXT: cw = o.ted.color; break;
    case G_BOX: case G_BOXCHAR: cw = uint16_t(o.spec); break;
    default: return true;
    }
    if ((cw & 15) > BLACK) return false;
    if (((cw >> 8) & 15) > BLACK) return false;
    return true;
}

void Aes::draw3dOutline(const Rect& r)
{
    auto pline = [&](const int* p, int col) {
        s_.line(p[0], p[1], p[2], p[3], MD_REPLACE, col);
        s_.line(p[2], p[3], p[4], p[5], MD_REPLACE, col);
    };
    int pts[6];
    pts[0] = r.x - 3; pts[1] = r.y + r.h + 2;
    pts[2] = pts[0] + r.w + 5; pts[3] = pts[1];
    pts[4] = pts[2]; pts[5] = r.y - 3;
    pline(pts, LBLACK);
    for (int i = 0; i < 2; i++) {
        pts[0]++; pts[1]--; pts[2]--; pts[3]--; pts[4]--; pts[5]++;
        pline(pts, LBLACK);
    }
    pts[0] = r.x - 3; pts[1] = r.y + r.h + 2;
    pts[2] = pts[0]; pts[3] = r.y - 3;
    pts[4] = pts[2] + r.w + 5; pts[5] = pts[3];
    pline(pts, LBLACK);
    for (int i = 0; i < 2; i++) {
        pts[0]++; pts[1]--; pts[2]++; pts[3]++; pts[4]--; pts[5]++;
        pline(pts, WHITE);
    }
}

void Aes::add3dEffect(const Rect& pt, int state, int th, int color)
{
    Rect r = pt;
    if (th > 0) r = r.inset(th);
    int pts[6];
    pts[0] = pts[2] = r.x;
    pts[3] = pts[5] = r.y;
    pts[1] = pts[3] + r.h - 2;
    pts[4] = pts[2] + r.w - 2;
    int dc = (state & SELECTED) ? BLACK : WHITE;
    s_.line(pts[0], pts[1], pts[2], pts[3], MD_REPLACE, dc);
    s_.line(pts[2], pts[3], pts[4], pts[5], MD_REPLACE, dc);
    pts[0]++; pts[1]++; pts[3] = pts[1]; pts[4]++; pts[2] = pts[4]; pts[5]++;
    dc = (state & SELECTED) ? LWHITE : LBLACK;
    if (dc == color || env_.ncolors() < dc) dc = (state & SELECTED) ? WHITE : BLACK;
    s_.line(pts[0], pts[1], pts[2], pts[3], MD_REPLACE, dc);
    s_.line(pts[2], pts[3], pts[4], pts[5], MD_REPLACE, dc);
}

// gr_gicon()
void Aes::drawIcon(int state, const IconBlk& ib, const CIcon* ci, int ox, int oy)
{
    int fg = (ib.ch >> 12) & 15, bg = (ib.ch >> 8) & 15;
    int c = ib.ch & 0xFF;
    Rect pi(ox + ib.xicon, oy + ib.yicon, ib.wicon, ib.hicon);
    Rect pt(ox + ib.xtext, oy + ib.ytext, ib.wtext, ib.htext);
    int wpr = (ib.wicon + 15) / 16;
    const std::vector<uint16_t>* data;
    const std::vector<uint16_t>* mask;
    std::vector<uint16_t> colData;
    int splanes = s_.planes;
    if (ci) {
        bool useSel = (state & SELECTED) && ci->hasSel;
        const std::vector<uint16_t>& src = useSel ? ci->sel : ci->col;
        mask = useSel ? &ci->selMask : &ci->colMask;
        // expand to screen depth like TOS: upper planes = AND of all source planes
        size_t plane = size_t(wpr) * ib.hicon;
        colData.assign(plane * splanes, 0);
        for (int p = 0; p < splanes; p++) {
            for (size_t k = 0; k < plane; k++) {
                uint16_t v;
                if (p < ci->planes) v = k + p * plane < src.size() ? src[k + p * plane] : 0;
                else {
                    v = 0xFFFF;
                    for (int q = 0; q < ci->planes; q++) v &= (k + q * plane < src.size() ? src[k + q * plane] : 0);
                }
                if (ci->planes != splanes) v &= (k < mask->size() ? (*mask)[k] : 0);
                colData[k + p * plane] = v;
            }
        }
        data = &colData;
    } else {
        if (state & SELECTED) std::swap(fg, bg);
        data = &ib.data.words;
        mask = &ib.mask.words;
    }
    if (!((state & WHITEBAK) && bg == WHITE)) {
        s_.bltMono(mask->data(), mask->size(), wpr, 0, 0, pi.x, pi.y, pi.w, pi.h, MD_TRANS, bg, fg);
        if (!ib.text.empty()) {
            if (ci && (state & SELECTED)) grRect(fg, IP_SOLID, pt);
            else grRect(bg, IP_SOLID, pt);
        }
    }
    if (ci) {
        s_.bltPlanesOr(*data, wpr, ib.hicon, splanes, pi.x, pi.y);
        if (state & SELECTED) {
            std::swap(fg, bg);
            if (!ci->hasSel) {
                std::vector<uint16_t> dm(mask->size());
                for (int y = 0; y < ib.hicon; y++)
                    for (int x = 0; x < wpr; x++) {
                        size_t k = size_t(y) * wpr + x;
                        if (k < dm.size()) dm[k] = (*mask)[k] & ((y & 1) ? 0xAAAA : 0x5555);
                    }
                s_.bltMono(dm.data(), dm.size(), wpr, 0, 0, pi.x, pi.y, pi.w, pi.h, MD_TRANS, BLACK, WHITE);
            }
        }
    } else {
        s_.bltMono(data->data(), data->size(), wpr, 0, 0, pi.x, pi.y, pi.w, pi.h, MD_TRANS, fg, bg);
    }
    if (c) {
        char cc = char(c);
        s_.text(*env_.small, pi.x + ib.xchar, pi.y + ib.ychar, &cc, 1, MD_TRANS, fg);
    }
    grText(TE_CNTR, FONT_SMALL, ib.text, pt, MD_TRANS, fg);
}

// ---------------------------------------------------------------- just_draw
void Aes::justDraw(const Tree& tree, int idx, const Rect& r)
{
    const Object& o = tree.obj[idx];
    const bool threeD = env_.use3D();
    int obtype = o.baseType();
    int flags = o.flags;
    int state = o.state;
    if (!threeD) flags &= ~FL3DMASK;
    if (flags & HIDETREE) return;
    if (typeHasBoxSpec(obtype) && o.spec == 0xFFFFFFFFu) return;
    if (obtype == G_CICON && !env_.aes.colourIcons()) obtype = -1;   // unknown to this AES
    if (obtype == G_SHORTCUT || obtype == G_WINTITLE) obtype = G_STRING;  // AES < MagiC: unknown
    if (obtype > G_CICON && env_.aes.version < 0x0400) obtype = -1;

    // ob_sst()
    int th = 0;
    char ch = 0;
    switch (obtype) {
    case G_TITLE: th = 1; break;
    case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT: th = o.ted.thickness; break;
    case G_BOX: case G_BOXCHAR: case G_IBOX: th = int8_t((o.spec >> 16) & 0xFF); ch = char(o.spec >> 24); break;
    case G_BUTTON:
        th--;
        if (flags & EXIT) th--;
        if (flags & DEFAULT) th--;
        break;
    }

    Rect t = r;
    int effectTh = th;
    bool movetext = false, changecol = false;
    if (threeD) {
        if (flags & FL3DOBJ) {
            t = t.inset(-ADJ3DSTD);
            if ((flags & FL3DMASK) == FL3DACT) { movetext = env_.actTxtMove; changecol = env_.actColChange; }
            else { movetext = env_.indTxtMove; changecol = env_.indColChange; }
        } else {
            changecol = !xorIsOk(obtype, flags, o);
        }
    }
    Rect effect = t;

    int bcol = BLACK, tcol = BLACK, ipat = IP_HOLLOW, icol = WHITE, tmode = MD_REPLACE;
    TedInfo ted = o.ted;

    if (obtype != G_STRING) {
        int tmpth = th < 0 ? 0 : th;
        tmode = MD_REPLACE;
        tcol = BLACK;
        switch (obtype) {
        case G_BOXTEXT: case G_FBOXTEXT: case G_TEXT: case G_FTEXT:
            crack(ted.color, bcol, tcol, ipat, icol, tmode);
            if (threeD && (flags & FL3DMASK) && ipat == IP_HOLLOW && icol == WHITE) {
                ipat = IP_SOLID;
                icol = env_.backgrCol;
                if (tmode == MD_REPLACE) {
                    tmode = MD_TRANS;
                    if (obtype == G_TEXT) { obtype = G_BOXTEXT; tmpth = th = 0; }
                    else if (obtype == G_FTEXT) { obtype = G_FBOXTEXT; tmpth = th = 0; }
                }
            }
            break;
        }
        bool boxy = false;
        switch (obtype) {
        case G_BOX: case G_BOXCHAR: case G_IBOX:
            crack(uint16_t(o.spec), bcol, tcol, ipat, icol, tmode);
            if (threeD && obtype != G_IBOX && ipat == IP_HOLLOW && icol == WHITE) {
                switch (flags & FL3DMASK) {
                case FL3DIND: ipat = IP_SOLID; icol = env_.indButCol; break;
                case FL3DACT: ipat = IP_SOLID; icol = env_.actButCol; break;
                case FL3DBAK: ipat = IP_SOLID; icol = env_.backgrCol; break;
                }
            }
            boxy = true;
            break;
        case G_BUTTON:
            bcol = BLACK;
            if (threeD && (flags & FL3DOBJ)) {
                ipat = IP_SOLID;
                icol = ((flags & FL3DMASK) == FL3DACT) ? env_.actButCol : env_.indButCol;
            } else {
                ipat = IP_HOLLOW;
                icol = WHITE;
            }
            boxy = true;
            break;
        case G_BOXTEXT: case G_FBOXTEXT:
            boxy = true;
            break;
        }
        if (boxy) {
            if (th != 0) grBox(t.x, t.y, t.w, t.h, th, bcol);
            if (obtype != G_IBOX) {
                Rect in = t.inset(tmpth);
                if (changecol && (state & SELECTED)) {
                    if (ipat == IP_HOLLOW) { ipat = IP_SOLID; icol = BLACK; }
                    else icol = xorColor(icol);
                }
                grRect(icol, ipat, in);
            }
        }
        if (changecol && (state & SELECTED)) {
            tmode = MD_TRANS;
            tcol = xorColor(tcol);
        }

        std::string text;
        int just = ted.just, fnt = ted.font;
        bool drawText = false;
        switch (obtype) {
        case G_FTEXT: case G_FBOXTEXT:
            text = formatText(ted.just, ted.text, ted.tmplt);
            drawText = true;
            break;
        case G_BOXCHAR:
            text = std::string(1, ch);
            just = TE_CNTR; fnt = FONT_IBM;
            drawText = true;
            break;
        case G_TEXT: case G_BOXTEXT:
            text = ted.text;
            drawText = true;
            break;
        case G_IMAGE: {
            int color = o.bit.color;
            if (threeD && (state & SELECTED)) {
                s_.fill(t.x, t.y, t.w, t.h, MD_XOR, FIS_SOLID, IP_SOLID, BLACK);
                color = xorColor(color);
            }
            const Bitmap& b = o.bit.img;
            s_.bltMono(b.words.data(), b.words.size(), b.wpr(), o.bit.x, o.bit.y, t.x, t.y, b.w, b.h, MD_TRANS,
                       color, WHITE);
            break;
        }
        case G_ICON: case G_CICON: {
            const CIcon* ci = nullptr;
            if (obtype == G_CICON) {
                // best_match(): same planes, else most planes below screen depth
                for (const CIcon& c : o.cicons) {
                    if (c.planes > s_.planes) continue;
                    if (c.planes == s_.planes) { ci = &c; break; }
                    if (!ci || c.planes > ci->planes) ci = &c;
                }
            }
            drawIcon(state, o.icon, ci, t.x, t.y);
            state &= ~SELECTED;
            break;
        }
        case G_USERDEF: {
            // a program supplied routine draws this; show a neutral placeholder
            s_.fill(t.x, t.y, t.w, t.h, MD_TRANS, FIS_PATTERN, 2, LBLACK < s_.ncolors() ? LBLACK : BLACK);
            s_.box(t, MD_REPLACE, BLACK);
            break;
        }
        }
        if (drawText) {
            Rect c = t.inset(tmpth);
            if (movetext) {
                if (state & SELECTED) c.x++;
                else c.y--;
            }
            grText(just, fnt, text, c, tmode, tcol);
        }
    }

    if (obtype == G_STRING || obtype == G_TITLE || obtype == G_BUTTON) {
        const std::string& str = o.str;
        int len = (int)str.size();
        if (len) {
            tcol = BLACK;
            if (threeD && obtype == G_BUTTON && changecol && (state & SELECTED)) tcol = WHITE;
            int tx = t.x;
            int ty = t.y + (t.h - env_.res.cellH) / 2;
            if (obtype == G_BUTTON) {
                tx += (t.w - len * env_.res.cellW) / 2;
                if (threeD && movetext && !(state & SELECTED)) { tx--; ty--; }
            }
            s_.text(*env_.ibm, tx, ty, str.data(), len, MD_TRANS, tcol);
        }
    }

    if (state) {
        if (state & OUTLINED) {
            if (threeD && (flags & FL3DMASK) == FL3DBAK) draw3dOutline(t);
            else {
                grBox(t.x - 3, t.y - 3, t.w + 6, t.h + 6, 1, BLACK);
                grBox(t.x - 2, t.y - 2, t.w + 4, t.h + 4, 2, WHITE);
            }
        }
        if (th > 0) t = t.inset(th);
        else th = -th;
        if ((state & SHADOWED) && th) {
            s_.fill(t.x, t.y + t.h + th, t.w + th, 2 * th, MD_REPLACE, FIS_SOLID, 0, bcol);
            s_.fill(t.x + t.w + th, t.y, 2 * th, t.h + 3 * th, MD_REPLACE, FIS_SOLID, 0, bcol);
        }
        if (state & CHECKED) {
            char c = 0x08;
            s_.text(*env_.ibm, t.x + 2, t.y, &c, 1, MD_TRANS, BLACK);
        }
        if (state & CROSSED) {
            s_.line(t.x, t.y, t.x + t.w - 1, t.y + t.h - 1, MD_TRANS, WHITE);
            s_.line(t.x, t.y + t.h - 1, t.x + t.w - 1, t.y, MD_TRANS, WHITE);
        }
        if (state & DISABLED) {
            int c = WHITE;
            if (threeD && (flags & FL3DMASK) == FL3DBAK) c = env_.backgrCol;
            s_.fill(t.x, t.y, t.w, t.h, MD_TRANS, FIS_PATTERN, IP_4PATT, c);
        }
        if (state & SELECTED) {
            if (!threeD || (!(flags & FL3DOBJ) && !changecol))
                s_.fill(t.x, t.y, t.w, t.h, MD_XOR, FIS_SOLID, IP_SOLID, BLACK);
        }
    }

    if (threeD && (flags & FL3DOBJ)) add3dEffect(effect, state, effectTh, icol);
}

void Aes::drawTree(const Tree& t, const Layout& L, int start, int depth)
{
    if (start < 0 || start >= (int)t.obj.size()) return;
    std::function<void(int, int)> walk = [&](int o, int d) {
        justDraw(t, o, L.abs[o]);
        if ((t.obj[o].flags & HIDETREE) || d >= depth) return;
        for (int c : childrenOf(t, o)) walk(c, d + 1);
    };
    walk(start, 0);
}

int Aes::find(const Tree& t, const Layout& L, int start, int depth, int mx, int my) const
{
    if (start < 0 || start >= (int)t.obj.size()) return -1;
    std::function<int(int, int)> rec = [&](int o, int d) -> int {
        if (!L.abs[o].contains(mx, my)) return -1;
        int best = o;
        if (d < depth)
            for (int c : childrenOf(t, o)) {
                if (t.obj[c].flags & HIDETREE) continue;
                int r = rec(c, d + 1);
                if (r >= 0) best = r;
            }
        return best;
    };
    return rec(start, 0);
}

// ---------------------------------------------------------------- desktop & menus
void Aes::drawDesktop()
{
    // the TOS desktop background: G_BOX, pattern 4, colour GREEN
    s_.resetClip();
    int col = s_.ncolors() >= 4 ? GREEN : BLACK;
    s_.fill(0, 0, s_.w, s_.h, MD_REPLACE, FIS_PATTERN, IP_4PATT, col);
}

void Aes::drawMenuBar(const Tree& menu, const Layout& L)
{
    MenuInfo mi;
    if (!analyseMenu(menu, mi)) return;
    drawTree(menu, L, mi.bar, MAX_DEPTH);
    int hb = env_.hbox();
    s_.line(0, hb - 1, env_.res.w - 1, hb - 1, MD_REPLACE, BLACK);
}

void Aes::drawDropdown(const Tree& menu, const Layout& L, int box)
{
    drawTree(menu, L, box, MAX_DEPTH);
}

// ---------------------------------------------------------------- alerts
AlertInfo Aes::parseAlert(const std::string& s)
{
    const int MAX_LINENUM = 5, MAX_LINELEN = 40, MAX_BUTNUM = 3, MAX_BUTLEN = 20;
    AlertInfo a;
    if (s.size() < 2) return a;
    a.icon = s[1] - '0';
    if (a.icon < 0 || a.icon > 9) a.icon = 0;
    size_t p = std::min<size_t>(3, s.size());
    auto endstring = [&](size_t i) { return i >= s.size() || s[i] == ']' || s[i] == 0; };
    auto endsub = [&](size_t i) { return i >= s.size() || s[i] == '|' || s[i] == ']' || s[i] == 0; };
    auto brk = [&](int maxnum, int maxlen, std::vector<std::string>& out, bool& tooMany, int& maxl) {
        if (p < s.size() && s[p] == '[') p++;
        for (int i = 0; i < maxnum; i++) {
            std::string part;
            for (int j = 0; j < maxlen; j++) {
                if (endsub(p)) {
                    if (p + 1 < s.size() && s[p] != 0 && s[p] == s[p + 1]) p++;
                    else break;
                }
                part += s[p++];
            }
            maxl = std::max(maxl, (int)part.size());
            out.push_back(part);
            if (!endsub(p)) while (!endsub(p)) p++;
            if (endstring(p)) break;
            p++;
            if (i == maxnum - 1) tooMany = true;
        }
        while (!endstring(p)) p++;
        if (p < s.size()) p++;
    };
    brk(MAX_LINENUM, MAX_LINELEN, a.lines, a.tooManyLines, a.maxLine);
    brk(MAX_BUTNUM, MAX_BUTLEN, a.buttons, a.tooManyButtons, a.maxButton);
    return a;
}

Tree Aes::buildAlert(const std::string& str, int defButton, Layout& L) const
{
    AlertInfo a = parseAlert(str);
    int nummsg = std::max<int>(1, (int)a.lines.size());
    int mlenmsg = a.maxLine;
    int numbut = std::max<int>(1, (int)a.buttons.size());
    int mlenbut = a.maxButton + 1;
    const bool threeD = env_.use3D();

    Rect al(0, 0, 1, 1), ms(1, 1, mlenmsg, 1), bt(1, 2 + nummsg, mlenbut, 1), ic(0, 0, 0, 0);
    if (a.icon) {
        int hicon = (32 + env_.res.cellH - 1) / env_.res.cellH;
        ic = Rect(1, 1, 4, hicon);
        al.w += ic.w + 1;
        ms.x = ic.x + ic.w + 1;
    }
    int allbut = numbut * mlenbut + 2 * (numbut - 1);
    if (mlenmsg + al.w > allbut + 1) {
        al.w += mlenmsg + 1;
        bt.x = (al.w - allbut) / 2;
    } else {
        al.w = allbut + 2;
        bt.x = 1;
    }
    bt.y = std::max(ic.y + ic.h, nummsg + 1) + 1;
    al.h = std::max(bt.y + bt.h, ic.y + ic.h) + 1;

    Tree t;
    t.kind = TreeKind::Dialog;
    Object root;
    root.type = G_BOX;
    root.flags = FL3DBAK;
    root.state = OUTLINED;
    root.spec = 0x00011100;
    if (threeD) {
        int color = env_.backgrCol < env_.ncolors() ? env_.backgrCol : WHITE;
        root.spec = (root.spec & 0xFFFFFF80u) | 0x70u | uint32_t(color & 15);
    }
    root.x = makeRaw(al.x, 0); root.y = makeRaw(al.y, 0); root.w = makeRaw(al.w, 0); root.h = makeRaw(al.h, 0);
    addObject(t, -1, root);
    int iconIdx = -1;
    if (a.icon) {
        Object io;
        io.type = G_IMAGE;
        io.bit.img.w = 32; io.bit.img.h = 32;
        int which = a.icon == 1 ? 0 : a.icon == 2 ? 1 : 2;
        io.bit.img.words.assign(alertIcon(which), alertIcon(which) + 64);
        io.bit.color = BLACK;
        io.x = makeRaw(ic.x, 0); io.y = makeRaw(ic.y, 0);
        io.w = makeRaw(0, 32); io.h = makeRaw(0, 32);
        iconIdx = addObject(t, 0, io);
    }
    for (size_t i = 0; i < a.lines.size(); i++) {
        Object m;
        m.type = G_STRING;
        m.str = a.lines[i];
        m.x = makeRaw(ms.x, 0); m.y = makeRaw(ms.y + int(i), 0); m.w = makeRaw(ms.w, 0); m.h = makeRaw(1, 0);
        addObject(t, 0, m);
    }
    for (size_t i = 0; i < a.buttons.size(); i++) {
        Object b;
        b.type = G_BUTTON;
        b.str = a.buttons[i];
        b.flags = SELECTABLE | EXIT | (threeD ? FL3DACT : 0);
        if (defButton == int(i) + 1) b.flags |= DEFAULT;
        b.x = makeRaw(bt.x, 0); b.y = makeRaw(bt.y, 0); b.w = makeRaw(bt.w, 0); b.h = makeRaw(1, 0);
        bt.x += mlenbut + 2;
        int idx = addObject(t, 0, b);
        if (threeD) {
            // TOS 4 makes alert buttons 3 pixels taller and moves them up
            int y = toPixels(t.obj[idx].y, env_.res.cellH) - ADJ3DSTD;
            int h = toPixels(t.obj[idx].h, env_.res.cellH) + 2 * ADJ3DSTD - 1;
            t.obj[idx].y = fromPixels(y, env_.res.cellH);
            t.obj[idx].h = fromPixels(h, env_.res.cellH);
        }
    }
    (void)iconIdx;
    // position: form_center()
    Layout tmp;
    layout(t, 0, 0, tmp);
    int x, y;
    center(tmp.abs[0].w, tmp.abs[0].h, x, y);
    layout(t, x, y, L);
    return t;
}

} // namespace gem
