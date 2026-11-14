#include "app/app.h"
#include "base/bytes.h"
#include <cctype>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <functional>

namespace app {

using namespace rsc;
using namespace platform;
namespace col = ui::col;

namespace {

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

int snapTo(int v, int cell) { return (int)std::lround(double(v) / cell) * cell; }

// visibility of an object in the composed scene (hidden parents, closed dropdowns)
bool visibleInScene(const Tree& t, const gem::Scene& sc, int o)
{
    MenuInfo mi;
    bool menu = sc.isMenu && analyseMenu(t, mi);
    for (int p = o; p >= 0; p = parentOf(t, p)) {
        if (p != o && (t.obj[p].flags & HIDETREE)) return false;
        if (menu) {
            if (p == mi.screen && o != mi.screen) return false;   // screen ibox: only via open box
            for (int b : mi.boxes)
                if (p == b && b != sc.openBox) return false;
            if (p == sc.openBox) return true;
        }
    }
    if (menu && o == mi.screen) return false;
    return true;
}

const char* kCtx[] = { "Edit image / icon...", "-", "Cut", "Copy", "Paste", "Duplicate", "Delete", "-",
                       "Bring forward", "Send backward", "Move into previous sibling", "Move out to parent", "-",
                       "Select parent", "Fit size to children", "Snap to character grid" };
} // namespace

void App::drawEditor(const Rect& r)
{
    ui::Canvas& c = canvas;
    c.fill(r, 0x6A6A6A);
    viewRect = r;
    if (fitPending && r.w > 64 && r.h > 64) {
        int z = 1;
        while (z < 8 && env.res.w * (z + 1) * env.res.aspectX + 32 <= r.w && env.res.h * (z + 1) * env.res.aspectY + 32 <= r.h) z++;
        set.zoom = z;
        fitPending = false;
    }
    int zx = set.zoom * env.res.aspectX, zy = set.zoom * env.res.aspectY;
    int sw = env.res.w * zx, sh = env.res.h * zy;
    bool hov = u.hover(r);

    // zoom / scroll with the wheel
    if (hov && u.in.wheelY != 0) {
        if (u.in.mods & MOD_CTRL) {
            int nz = std::clamp(set.zoom + (u.in.wheelY < 0 ? 1 : -1), 1, 8);
            set.zoom = nz;
        } else if (u.in.mods & MOD_SHIFT) {
            scrollX += int(u.in.wheelY * 60);
        } else {
            scrollY += int(u.in.wheelY * 60);
        }
        u.in.wheelY = 0;
        zx = set.zoom * env.res.aspectX; zy = set.zoom * env.res.aspectY;
        sw = env.res.w * zx; sh = env.res.h * zy;
    }
    if (hov && u.in.wheelX != 0) { scrollX += int(u.in.wheelX * 60); u.in.wheelX = 0; }
    int margin = 16;
    scrollX = std::clamp(scrollX, 0, std::max(0, sw + 2 * margin - r.w));
    scrollY = std::clamp(scrollY, 0, std::max(0, sh + 2 * margin - r.h));
    int ox = sw + 2 * margin <= r.w ? r.x + (r.w - sw) / 2 : r.x + margin - scrollX;
    int oy = sh + 2 * margin <= r.h ? r.y + (r.h - sh) / 2 : r.y + margin - scrollY;
    viewOx = ox; viewOy = oy; viewZx = zx; viewZy = zy;
    int ax = floorDiv(u.in.mx - ox, zx), ay = floorDiv(u.in.my - oy, zy);

    // ---------------------------------------------------------------- compose the Atari screen
    gem::Aes aes(screen, env);
    gem::SceneOptions opt;
    opt.openMenu = openMenuTitle;
    opt.alertDefault = alertDefault;
    Tree* t = curTree();
    if (t) {
        if (testMode) {
            Tree tt = testTree;
            MenuInfo mi;
            if (tt.kind == TreeKind::Menu && analyseMenu(tt, mi) && hov) {
                // AES menu tracking: hover opens titles and highlights items
                gem::Layout L;
                aes.layout(tt, 0, 0, L);
                int o = aes.find(tt, L, mi.bar, gem::MAX_DEPTH, ax, ay);
                for (size_t i = 0; i < mi.titles.size(); i++)
                    if (o == mi.titles[i]) openMenuTitle = int(i);
                if (openMenuTitle >= 0 && openMenuTitle < (int)mi.boxes.size()) {
                    int box = mi.boxes[openMenuTitle];
                    int it = aes.find(tt, L, box, gem::MAX_DEPTH, ax, ay);
                    if (it > 0 && it != box && !(tt.obj[it].state & DISABLED)) tt.obj[it].state |= SELECTED;
                }
                opt.openMenu = openMenuTitle;
            }
            scene = gem::renderTree(aes, tt, opt);
            // text cursor like objc_edit(): XOR line taller than the character
            if (testEdit >= 0 && testEdit < (int)scene.tree.obj.size() && testEdit < (int)scene.L.abs.size()) {
                const Object& o = scene.tree.obj[testEdit];
                if (typeHasTedinfo(o.baseType())) {
                    int th = o.ted.thickness > 0 && (o.baseType() == G_FBOXTEXT || o.baseType() == G_BOXTEXT) ? o.ted.thickness : 0;
                    Rect a = scene.L.abs[testEdit].inset(th);
                    const gem::SysFont& f = o.ted.font == FONT_SMALL ? *env.small : *env.ibm;
                    int tl = (int)o.ted.tmplt.size();
                    int tw = std::min(a.w, tl * f.cw);
                    int x = a.x, y = a.y;
                    if (a.h - f.ch > 0) y += (a.h - f.ch + 1) / 2;
                    if (a.w - tw > 0) x += o.ted.just == TE_CNTR ? (a.w - tw + 1) / 2 : o.ted.just == TE_RIGHT ? a.w - tw : 0;
                    int n = 0, pos = tl;
                    for (int i = 0; i < tl; i++)
                        if (o.ted.tmplt[i] == '_') {
                            if (n == testCursor) { pos = i; break; }
                            n++;
                        }
                    if (n < testCursor || pos == tl) {
                        // after the last input character
                        int last = (int)o.ted.tmplt.find_last_of('_');
                        pos = last >= 0 && testCursor > 0 ? last + 1 : 0;
                    }
                    int cx = x + pos * f.cw;
                    screen.line(cx, y - 3, cx, y + f.ch + 2, gem::MD_XOR, gem::BLACK);
                }
            }
        } else {
            scene = gem::renderTree(aes, *t, opt);
        }
    } else {
        scene = gem::renderItem(aes, doc, selKind, selItem, opt);
    }

    c.pushClip(r);
    c.fill(Rect(ox + 6, oy + 6, sw, sh), 0x404040);
    c.blitScreen(screen, ox, oy, zx, zy);
    auto toHost = [&](const Rect& a) { return Rect(ox + a.x * zx, oy + a.y * zy, a.w * zx, a.h * zy); };

    if (t && !testMode && !scene.tree.obj.empty() && scene.L.abs.size() == t->obj.size()) {
        const gem::Layout& L = scene.L;
        // character grid
        if (set.grid) {
            int cw = cellW() * zx, ch = cellH() * zy;
            if (cw >= 4 && ch >= 4) {
                for (int x = ox; x < ox + sw; x += cw) c.blend(Rect(x, oy, 1, sh), col::grid, 90);
                for (int y = oy; y < oy + sh; y += ch) c.blend(Rect(ox, y, sw, 1), col::grid, 90);
            }
        }
        // outlines of invisible objects
        if (set.outlines)
            for (size_t i = 0; i < t->obj.size(); i++) {
                const Object& o = t->obj[i];
                if (!visibleInScene(*t, scene, int(i))) continue;
                bool invisible = o.baseType() == G_IBOX && int8_t((o.spec >> 16) & 0xFF) == 0;
                invisible |= o.baseType() == G_STRING || o.baseType() == G_TEXT || o.baseType() == G_FTEXT ||
                             o.baseType() == G_TITLE || (o.flags & HIDETREE);
                if (invisible) c.dashed(toHost(L.abs[i]), 0x00B0B0, 0xFFFFFF);
            }

        // picking
        auto pick = [&](int px, int py) -> int {
            MenuInfo mi;
            if (scene.isMenu && analyseMenu(*t, mi)) {
                if (scene.openBox >= 0) {
                    int o = aes.find(*t, L, scene.openBox, gem::MAX_DEPTH, px, py);
                    if (o >= 0) return o;
                }
                int o = aes.find(*t, L, mi.bar, gem::MAX_DEPTH, px, py);
                if (o >= 0) return o;
                return -1;
            }
            return aes.find(*t, L, 0, gem::MAX_DEPTH, px, py);
        };
        hoverObj = hov ? pick(ax, ay) : -1;
        if (hoverObj >= 0 && !isSelected(hoverObj)) c.frame(toHost(L.abs[hoverObj]).inset(-1), col::accent2);

        // selection & handles
        int prim = primaryObj();
        Rect handles[8];
        bool menuTree = scene.isMenu;
        for (int o : selObjs) {
            if (o < 0 || o >= (int)L.abs.size() || !visibleInScene(*t, scene, o)) continue;
            Rect hr = toHost(L.abs[o]).inset(-1);
            c.dashed(hr, col::accent, 0xFFFFFF, 0);
            c.frame(hr.inset(-1), o == prim ? col::accent : 0xFF9AD0);
        }
        bool canResize = prim >= 0 && prim < (int)L.abs.size() && !menuTree && visibleInScene(*t, scene, prim);
        if (canResize) {
            Rect hr = toHost(L.abs[prim]).inset(-2);
            int hx[3] = { hr.x - 3, hr.x + hr.w / 2 - 3, hr.x + hr.w - 3 };
            int hy[3] = { hr.y - 3, hr.y + hr.h / 2 - 3, hr.y + hr.h - 3 };
            const int map[8][2] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 1 }, { 2, 2 }, { 1, 2 }, { 0, 2 }, { 0, 1 } };
            for (int i = 0; i < 8; i++) {
                handles[i] = Rect(hx[map[i][0]], hy[map[i][1]], 7, 7);
                bool rootMoveHandle = prim == 0 && (i == 0 || i == 1 || i == 2 || i == 6 || i == 7);
                if (rootMoveHandle) continue;
                c.fill(handles[i], 0xFFFFFF);
                c.frame(handles[i], col::accent);
            }
        }

        // cursor feedback
        int overHandle = -1;
        if (canResize && hov)
            for (int i = 0; i < 8; i++) {
                bool rootMoveHandle = prim == 0 && (i == 0 || i == 1 || i == 2 || i == 6 || i == 7);
                if (!rootMoveHandle && handles[i].inset(-2).contains(u.in.mx, u.in.my)) overHandle = i;
            }
        if (overHandle >= 0 || drag.kind == DragState::Resize) {
            int h = drag.kind == DragState::Resize ? drag.handle : overHandle;
            cursor_ = (h == 0 || h == 4) ? Cursor::ResizeNWSE : (h == 2 || h == 6) ? Cursor::ResizeNESW
                    : (h == 1 || h == 5) ? Cursor::ResizeNS : Cursor::ResizeEW;
        } else if (drag.kind == DragState::Move || (hoverObj > 0 && isSelected(hoverObj) && !menuTree)) {
            cursor_ = Cursor::Move;
        }

        // ---------------------------------------------------------------- mouse
        if (hov && u.in.pressed[0]) {
            drag = DragState();
            drag.startAx = ax; drag.startAy = ay;
            if (overHandle >= 0) {
                drag.kind = DragState::Resize;
                drag.handle = overHandle;
                drag.objs = { prim };
                drag.orig = { L.rel[prim] };
            } else {
                int o = hoverObj;
                bool toggle = u.in.mods & (MOD_CTRL | MOD_SHIFT);
                if (o < 0) {
                    if (!toggle) selObjs.clear();
                    if (scene.isMenu) openMenuTitle = -1;
                } else {
                    if (toggle) selectObj(o, true);
                    else if (!isSelected(o)) selectObj(o);
                    else { selObjs.erase(std::find(selObjs.begin(), selObjs.end(), o)); selObjs.push_back(o); }
                    if (u.in.dbl) {
                        const Object& ob = t->obj[o];
                        if (ob.baseType() == G_IMAGE || ob.baseType() == G_ICON || ob.baseType() == G_CICON) editBitmap();
                        else u.focusField(fieldId("text"));
                    } else if (!menuTree) {
                        drag.kind = DragState::Move;
                        for (int s : selObjs)
                            if (s > 0) { drag.objs.push_back(s); drag.orig.push_back(L.rel[s]); }
                        if (drag.objs.empty()) drag.kind = DragState::None;
                    }
                }
            }
        }
        if (hov && u.in.pressed[2]) {
            drag = DragState();
            drag.kind = DragState::Pan;
            drag.startMx = u.in.mx; drag.startMy = u.in.my;
            drag.startScrollX = scrollX; drag.startScrollY = scrollY;
        }
        if (drag.kind == DragState::Pan) {
            cursor_ = Cursor::Move;
            if (u.in.down[2]) {
                scrollX = drag.startScrollX - (u.in.mx - drag.startMx);
                scrollY = drag.startScrollY - (u.in.my - drag.startMy);
            } else drag.kind = DragState::None;
        }
        if ((drag.kind == DragState::Move || drag.kind == DragState::Resize) && u.in.down[0]) {
            int dx = ax - drag.startAx, dy = ay - drag.startAy;
            if (!drag.moved && (std::abs(dx) > 0 || std::abs(dy) > 0)) {
                drag.moved = true;
                checkpoint();
            }
            if (drag.moved) {
                bool snap = set.snap && !(u.in.mods & MOD_ALT);
                int cw = cellW(), ch = cellH();
                for (size_t k = 0; k < drag.objs.size(); k++) {
                    int o = drag.objs[k];
                    if (o >= (int)t->obj.size()) continue;
                    Object& ob = t->obj[o];
                    Rect a = drag.orig[k];
                    if (drag.kind == DragState::Move) {
                        int nx = a.x + dx, ny = a.y + dy;
                        if (snap) { nx = snapTo(nx, cw); ny = snapTo(ny, ch); }
                        ob.x = fromPixels(nx, cw);
                        ob.y = fromPixels(ny, ch);
                    } else {
                        int x1 = a.x, y1 = a.y, x2 = a.x + a.w, y2 = a.y + a.h;
                        int hnd = drag.handle;
                        if (hnd == 0 || hnd == 6 || hnd == 7) x1 += dx;
                        if (hnd == 2 || hnd == 3 || hnd == 4) x2 += dx;
                        if (hnd == 0 || hnd == 1 || hnd == 2) y1 += dy;
                        if (hnd == 4 || hnd == 5 || hnd == 6) y2 += dy;
                        if (snap) { x1 = snapTo(x1, cw); x2 = snapTo(x2, cw); y1 = snapTo(y1, ch); y2 = snapTo(y2, ch); }
                        if (x2 - x1 < 1) { if (hnd == 0 || hnd == 6 || hnd == 7) x1 = x2 - 1; else x2 = x1 + 1; }
                        if (y2 - y1 < 1) { if (hnd == 0 || hnd == 1 || hnd == 2) y1 = y2 - 1; else y2 = y1 + 1; }
                        if (o != 0) { ob.x = fromPixels(x1, cw); ob.y = fromPixels(y1, ch); }
                        ob.w = fromPixels(x2 - x1, cw);
                        ob.h = fromPixels(y2 - y1, ch);
                    }
                }
            }
        }
        if (u.in.released[0] && (drag.kind == DragState::Move || drag.kind == DragState::Resize)) {
            if (drag.moved) setStatus(drag.kind == DragState::Move ? "Moved" : "Resized");
            drag.kind = DragState::None;
        }

        // context menu
        ui::Id ctxId = ui::Ui::hash("editor.ctx");
        if (hov && u.in.released[1] && !u.isPopupOpen()) {
            if (hoverObj >= 0 && !isSelected(hoverObj)) selectObj(hoverObj);
            std::vector<std::string> items(std::begin(kCtx), std::end(kCtx));
            std::vector<bool> en(items.size(), primaryObj() > 0);
            int p = primaryObj();
            int ty = p >= 0 ? t->obj[p].baseType() : -1;
            en[0] = ty == G_IMAGE || ty == G_ICON || ty == G_CICON;
            en[4] = clipboard.obj.size() > 1;
            en[14] = p >= 0;
            en[15] = p >= 0;
            u.openMenu(ctxId, u.in.mx, u.in.my, items, en);
        }
        switch (u.menuResult(ctxId)) {
        case 0: editBitmap(); break;
        case 2: cutSelection(); break;
        case 3: copySelection(); break;
        case 4: paste(); break;
        case 5: duplicateSelection(); break;
        case 6: deleteSelection(); break;
        case 8: reorderSelection(1); break;
        case 9: reorderSelection(-1); break;
        case 10: moveIntoPrevSibling(); break;
        case 11: moveOutToParent(); break;
        case 13: if (primaryObj() > 0) selectObj(parentOf(*t, primaryObj())); break;
        case 14: fitParent(); break;
        case 15: alignSelection(6); break;
        default: break;
        }
    } else if (t && testMode) {
        cursor_ = hov ? Cursor::Pointer : Cursor::Default;
        if (hov && u.in.pressed[0]) {
            int o = -1;
            MenuInfo mi;
            if (scene.isMenu && analyseMenu(testTree, mi)) {
                if (scene.openBox >= 0) o = gem::Aes(screen, env).find(testTree, scene.L, scene.openBox, gem::MAX_DEPTH, ax, ay);
                if (o < 0) o = gem::Aes(screen, env).find(testTree, scene.L, mi.bar, gem::MAX_DEPTH, ax, ay);
            } else {
                o = gem::Aes(screen, env).find(testTree, scene.L, 0, gem::MAX_DEPTH, ax, ay);
            }
            testClick(o);
        }
    } else {
        hoverObj = -1;
    }
    c.popClip();
    // frame around the view
    c.frame(Rect(r.x, r.y, r.w, r.h), col::line);
}

// ================================================================= test mode (form_do emulation)
void App::testClick(int obj)
{
    Tree& t = testTree;
    MenuInfo mi;
    if (t.kind == TreeKind::Menu && analyseMenu(t, mi)) {
        for (size_t i = 0; i < mi.titles.size(); i++)
            if (obj == mi.titles[i]) { openMenuTitle = openMenuTitle == int(i) ? -1 : int(i); return; }
        if (obj > 0 && openMenuTitle >= 0 && openMenuTitle < (int)mi.boxes.size() &&
            isAncestor(t, mi.boxes[openMenuTitle], obj) && !(t.obj[obj].state & DISABLED)) {
            int title = mi.titles[openMenuTitle];
            setStatus("MN_SELECTED: title #" + std::to_string(title) + ", item #" + std::to_string(obj) +
                      (t.obj[obj].name.empty() ? "" : " (" + t.obj[obj].name + ")"));
        }
        openMenuTitle = -1;
        return;
    }
    if (obj < 0) return;
    Object& o = t.obj[obj];
    if (o.flags & EDITABLE && !(o.state & DISABLED)) {
        testEdit = obj;
        std::string& s = o.ted.text;
        if (!s.empty() && s[0] == '@') s.clear();
        testCursor = (int)s.size();
    }
    if (o.state & DISABLED) return;
    if (o.flags & SELECTABLE) {
        if (o.flags & RBUTTON) {
            int par = parentOf(t, obj);
            for (int k : childrenOf(t, par))
                if (t.obj[k].flags & RBUTTON) t.obj[k].state &= ~SELECTED;
            o.state |= SELECTED;
        } else {
            o.state ^= SELECTED;
        }
    }
    if (o.flags & (EXIT | TOUCHEXIT)) {
        setStatus(std::string("form_do() returns #") + std::to_string(obj) +
                  (o.name.empty() ? "" : " (" + o.name + ")") + ((o.flags & EXIT) ? " - EXIT" : " - TOUCHEXIT"));
        if (o.flags & EXIT) {
            // a real dialog closes here; keep the state visible and reset later
        }
    } else if (o.flags & SELECTABLE) {
        setStatus("#" + std::to_string(obj) + ((o.state & SELECTED) ? " selected" : " deselected"));
    }
}

static bool validChar(char v, char& ch)
{
    unsigned char c = (unsigned char)ch;
    auto up = [&]() { if (ch >= 'a' && ch <= 'z') ch = char(ch - 32); };
    switch (v) {
    case '9': return c >= '0' && c <= '9';
    case 'A': up(); return (ch >= 'A' && ch <= 'Z') || ch == ' ';
    case 'a': return isalpha(c) || c == ' ' || c >= 0x80;
    case 'N': up(); return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ';
    case 'n': return isalnum(c) || c == ' ' || c >= 0x80;
    case 'F': up(); [[fallthrough]];
    case 'f': return isalnum((unsigned char)ch) || strchr("_!@#$%^&()+-=~`;'\",<>|[]{}?*:", ch);
    case 'P': up(); [[fallthrough]];
    case 'p': return isalnum((unsigned char)ch) || strchr("_!@#$%^&()+-=~`;'\",<>|[]{}?*:\\.", ch);
    case 'X': up(); return true;
    default: return c >= 32;
    }
}

void App::testKey(const KeyEvent& e)
{
    Tree& t = testTree;
    auto nextEditable = [&](int from, int dir) {
        int n = (int)t.obj.size();
        for (int i = 1; i <= n; i++) {
            int k = ((from + dir * i) % n + n) % n;
            if ((t.obj[k].flags & EDITABLE) && !(t.obj[k].flags & HIDETREE)) return k;
        }
        return from;
    };
    auto pressDefault = [&]() {
        for (size_t i = 0; i < t.obj.size(); i++)
            if (t.obj[i].flags & DEFAULT) {
                t.obj[i].state |= SELECTED;
                setStatus("form_do() returns DEFAULT #" + std::to_string(i) +
                          (t.obj[i].name.empty() ? "" : " (" + t.obj[i].name + ")"));
                return;
            }
        setStatus("No DEFAULT object: Return does nothing");
    };
    if (testEdit < 0 || testEdit >= (int)t.obj.size() || !typeHasTedinfo(t.obj[testEdit].baseType())) {
        if (e.sym == K_ESCAPE) enterTestMode(false);
        else if (e.sym == K_RETURN || e.sym == K_KP_ENTER) pressDefault();
        return;
    }
    TedInfo& te = t.obj[testEdit].ted;
    std::string& s = te.text;
    int maxLen = (int)std::count(te.tmplt.begin(), te.tmplt.end(), '_');
    testCursor = std::clamp(testCursor, 0, (int)s.size());
    switch (e.sym) {
    case K_LEFT: if (testCursor > 0) testCursor--; break;
    case K_RIGHT: if (testCursor < (int)s.size()) testCursor++; break;
    case K_HOME: testCursor = 0; break;
    case K_END: testCursor = (int)s.size(); break;
    case K_BACKSPACE: if (testCursor > 0) { s.erase(size_t(testCursor - 1), 1); testCursor--; } break;
    case K_DELETE: if (testCursor < (int)s.size()) s.erase(size_t(testCursor), 1); break;
    case K_ESCAPE:
        if (s.empty()) enterTestMode(false);
        else { s.clear(); testCursor = 0; }
        break;
    case K_TAB: case K_DOWN:
        testEdit = nextEditable(testEdit, (e.mods & MOD_SHIFT) ? -1 : 1);
        testCursor = (int)t.obj[testEdit].ted.text.size();
        break;
    case K_ISO_LEFT_TAB: case K_UP:
        testEdit = nextEditable(testEdit, -1);
        testCursor = (int)t.obj[testEdit].ted.text.size();
        break;
    case K_RETURN: case K_KP_ENTER: pressDefault(); break;
    default:
        if (!e.text.empty()) {
            std::string a = base::utf8ToAtari(e.text);
            for (char ch : a) {
                if ((int)s.size() >= maxLen) break;
                int vi = std::min(testCursor, std::max(0, (int)te.valid.size() - 1));
                char v = te.valid.empty() ? 'X' : te.valid[size_t(vi)];
                if (!validChar(v, ch)) continue;
                s.insert(size_t(testCursor), 1, ch);
                testCursor++;
            }
        }
    }
}

} // namespace app
