#include "app/app.h"
#include "base/bytes.h"
#include <algorithm>
#include <functional>

namespace app {

using namespace rsc;
using namespace platform;
namespace col = ui::col;

namespace {
constexpr int MENU_H = 20, TOOL_H = 30, STATUS_H = 22, LEFT_W = 290, RIGHT_W = 350, ROW_H = 18;

const char* kMenuTitles[] = { "File", "Edit", "Resource", "Object", "View", "Help" };
enum { M_FILE, M_EDIT, M_RES, M_OBJ, M_VIEW, M_HELP, M_COUNT };
} // namespace

// ================================================================= frame
void App::frame()
{
    ui::Canvas& c = canvas;
    int W = c.w(), H = c.h();
    c.fill(Rect(0, 0, W, H), col::bg);
    u.beginFrame();
    bool modalOpen = !modalStack.empty();
    u.enabled = !modalOpen;

    cursor_ = Cursor::Default;
    Rect body(0, MENU_H + TOOL_H, W, H - MENU_H - TOOL_H - STATUS_H);
    Rect left(0, body.y, LEFT_W, body.h);
    Rect right(W - RIGHT_W, body.y, RIGHT_W, body.h);
    Rect center(LEFT_W, body.y, W - LEFT_W - RIGHT_W, body.h);

    drawEditor(center);
    int resH = std::max(120, left.h * 2 / 5);
    drawResourcePanel(Rect(left.x, left.y, left.w, resH));
    drawOutlinePanel(Rect(left.x, left.y + resH, left.w, left.h - resH));
    int palH = 0;
    if (selKind == ItemKind::Tree && curTree()) {
        palH = 20 + 5 * 24 + 6;
        drawPalette(Rect(right.x, right.y, right.w, palH));
    }
    Rect insp(right.x, right.y + palH, right.w, right.h - palH);
    c.fill(insp, col::panel);
    c.vline(insp.x, insp.y, insp.y + insp.h, col::line);
    u.header(Rect(insp.x, insp.y, insp.w, 20), "INSPECTOR");
    Rect inner(insp.x + 1, insp.y + 20, insp.w - 14, insp.h - 20);
    c.pushClip(inner);
    int contentH = drawInspector(Rect(inner.x + 8, inner.y + 6 - inspectorScroll, inner.w - 14, 0), inspectorScroll);
    c.popClip();
    if (u.hover(inner) && u.in.wheelY != 0) {
        inspectorScroll += int(u.in.wheelY * 40);
        u.in.wheelY = 0;
    }
    u.scrollbar(u.id("insp.sb"), Rect(insp.x + insp.w - 13, inner.y, 13, inner.h), contentH + 20, inner.h, inspectorScroll);

    drawToolbar(Rect(0, MENU_H, W, TOOL_H));
    drawStatus(Rect(0, H - STATUS_H, W, STATUS_H));
    drawMenuBar(Rect(0, 0, W, MENU_H));

    if (!modalOpen) handleShortcuts();

    if (modalOpen) {
        u.enabled = false;
        c.blend(Rect(0, 0, W, H), 0x000000, 70);
        std::vector<Modal*> live;
        for (auto& m : modalStack) live.push_back(m.get());
        std::vector<Modal*> closed;
        for (size_t i = 0; i < live.size(); i++) {
            u.enabled = (i + 1 == live.size());
            if (!live[i]->draw(*this, u)) closed.push_back(live[i]);
        }
        for (Modal* m : closed)
            modalStack.erase(std::remove_if(modalStack.begin(), modalStack.end(),
                                            [m](const std::unique_ptr<Modal>& p) { return p.get() == m; }),
                             modalStack.end());
        if (!closed.empty()) redraw_ = true;
    }
    u.enabled = true;
    u.endFrame();
}

void App::pushModal(Modal* m)
{
    modalStack.emplace_back(m);
    u.dropFocus();
    u.popup = 0;
    redraw_ = true;
}

// ================================================================= menu bar
void App::drawMenuBar(const Rect& r)
{
    ui::Canvas& c = canvas;
    c.fill(r, col::header);
    c.hline(r.x, r.x + r.w - 1, r.y + r.h - 1, col::line);
    int x = 8;
    ui::Id ids[M_COUNT];
    Rect rects[M_COUNT];
    for (int i = 0; i < M_COUNT; i++) {
        std::string t = std::string(" ") + kMenuTitles[i] + " ";
        rects[i] = Rect(x, r.y, ui::Canvas::textW(t), r.h - 1);
        ids[i] = ui::Ui::hash(std::string("menu.") + kMenuTitles[i]);
        x += rects[i].w;
    }
    Tree* t = curTree();
    bool isMenuTree = t && t->kind == TreeKind::Menu;
    std::vector<std::vector<std::string>> items(M_COUNT);
    std::vector<std::vector<bool>> en(M_COUNT);
    auto add = [&](int m, const std::string& s, bool e = true) { items[m].push_back(s); en[m].push_back(e); };
    bool hasObj = t && primaryObj() >= 0;
    bool hasNonRoot = t && primaryObj() > 0;

    add(M_FILE, "New                  Ctrl+N");
    add(M_FILE, "Open...              Ctrl+O");
    add(M_FILE, "Save                 Ctrl+S");
    add(M_FILE, "Save as...     Ctrl+Shift+S");
    add(M_FILE, "-");
    add(M_FILE, "Import names (.HRD/.DEF/.H)...");
    add(M_FILE, "Export C header (.H)...");
    add(M_FILE, "Export Pascal include (.I)...");
    add(M_FILE, "Export GFA BASIC (.LST)...");
    add(M_FILE, "Export assembler (.S)...");
    add(M_FILE, "Export Modula-2 (.M2)...");
    add(M_FILE, "Export RSH C source...");
    add(M_FILE, "Export HRD names...");
    add(M_FILE, "Export DEF names (RCS 1)...");
    add(M_FILE, "-");
    add(M_FILE, "Save options...");
    add(M_FILE, "-");
    for (auto& f : set.recent) add(M_FILE, "  " + base::utf8ToAtari(base::pathBase(f)) + "  (" + base::utf8ToAtari(base::pathBase(base::pathDir(f))) + ")");
    if (!set.recent.empty()) add(M_FILE, "-");
    add(M_FILE, "Quit                 Ctrl+Q");

    add(M_EDIT, "Undo                 Ctrl+Z", canUndo());
    add(M_EDIT, "Redo                 Ctrl+Y", canRedo());
    add(M_EDIT, "-");
    add(M_EDIT, "Cut                  Ctrl+X", hasNonRoot);
    add(M_EDIT, "Copy                 Ctrl+C", hasNonRoot);
    add(M_EDIT, "Paste                Ctrl+V", t && clipboard.obj.size() > 1);
    add(M_EDIT, "Duplicate            Ctrl+D", hasNonRoot);
    add(M_EDIT, "Delete               Del", hasNonRoot);
    add(M_EDIT, "-");
    add(M_EDIT, "Select parent        Backspace", hasNonRoot);
    add(M_EDIT, "Select all children  Ctrl+A", hasObj);

    add(M_RES, "New dialog tree");
    add(M_RES, "New menu tree");
    add(M_RES, "New alert");
    add(M_RES, "New free string");
    add(M_RES, "New free image");
    add(M_RES, "-");
    add(M_RES, "Duplicate item", selItem >= 0);
    add(M_RES, "Delete item", selItem >= 0);
    add(M_RES, "Move item up         PgUp+Ctrl", selItem > 0);
    add(M_RES, "Move item down       PgDn+Ctrl", selItem >= 0);
    add(M_RES, "-");
    add(M_RES, "Renumber objects (tree order)", t != nullptr);
    add(M_RES, "Auto-name interactive objects", t != nullptr);
    add(M_RES, "Check names");

    add(M_OBJ, "Bring forward        Ctrl+F", hasNonRoot);
    add(M_OBJ, "Send backward        Ctrl+B", hasNonRoot);
    add(M_OBJ, "Bring to front", hasNonRoot);
    add(M_OBJ, "Send to back", hasNonRoot);
    add(M_OBJ, "-");
    add(M_OBJ, "Move into previous sibling", hasNonRoot);
    add(M_OBJ, "Move out to parent", hasNonRoot);
    add(M_OBJ, "-");
    add(M_OBJ, "Centre horizontally in parent", hasNonRoot);
    add(M_OBJ, "Centre vertically in parent", hasNonRoot);
    add(M_OBJ, "Align left to primary", selObjs.size() > 1);
    add(M_OBJ, "Align top to primary", selObjs.size() > 1);
    add(M_OBJ, "Align right to primary", selObjs.size() > 1);
    add(M_OBJ, "Same size as primary", selObjs.size() > 1);
    add(M_OBJ, "Snap to character grid", hasObj);
    add(M_OBJ, "Fit size to children", hasObj);
    add(M_OBJ, "-");
    add(M_OBJ, "Edit image / icon...       Ctrl+E", hasObj || selKind == ItemKind::Image);
    add(M_OBJ, "-");
    add(M_OBJ, "Menu: add title", isMenuTree);
    add(M_OBJ, "Menu: add item", isMenuTree);
    add(M_OBJ, "Menu: add separator", isMenuTree);
    add(M_OBJ, "Menu: delete title", isMenuTree);
    add(M_OBJ, "Menu: recalculate layout", isMenuTree);

    add(M_VIEW, "Zoom in              Ctrl++");
    add(M_VIEW, "Zoom out             Ctrl+-");
    add(M_VIEW, "-");
    add(M_VIEW, std::string(set.grid ? "\x08 " : "  ") + "Character grid       Ctrl+G");
    add(M_VIEW, std::string(set.snap ? "\x08 " : "  ") + "Snap to characters");
    add(M_VIEW, std::string(set.outlines ? "\x08 " : "  ") + "Show invisible outlines");
    add(M_VIEW, "-");
    add(M_VIEW, std::string(testMode ? "\x08 " : "  ") + "Test mode (form_do)  F5");

    add(M_HELP, "Keyboard shortcuts   F1");
    add(M_HELP, "About GEMWay");

    // title bar & switching
    int open = -1;
    for (int i = 0; i < M_COUNT; i++) if (u.popup == ids[i]) open = i;
    for (int i = 0; i < M_COUNT; i++) {
        bool over = u.enabled && rects[i].contains(u.in.mx, u.in.my);
        if (open == i) c.fill(rects[i], col::selBg);
        c.text(rects[i].x, r.y + 2, std::string(" ") + kMenuTitles[i] + " ", open == i ? col::selText : col::text);
        if (over && u.in.pressed[0] && open != i) {
            u.openMenu(ids[i], rects[i].x, rects[i].y + rects[i].h, items[i], en[i]);
            u.consumeClick();
        } else if (over && open >= 0 && open != i) {
            u.openMenu(ids[i], rects[i].x, rects[i].y + rects[i].h, items[i], en[i]);
        }
    }
    c.text(r.x + r.w - 8 * 26, r.y + 2, "GEM Resource Designer", col::dim);

    for (int i = 0; i < M_COUNT; i++) {
        int sel = u.menuResult(ids[i]);
        if (sel < 0) continue;
        const std::string& it = items[i][size_t(sel)];
        switch (i) {
        case M_FILE: {
            int recentStart = 17;
            if (sel == 0) confirmDiscard([this]() { newDoc(); });
            else if (sel == 1) cmdOpen();
            else if (sel == 2) cmdSave();
            else if (sel == 3) cmdSaveAs();
            else if (sel == 5) cmdImportNames();
            else if (sel >= 6 && sel <= 13) cmdExport(sel - 6);
            else if (sel == 15) pushModal(makeSettingsDialog(*this));
            else if (sel >= recentStart && sel < recentStart + (int)set.recent.size()) {
                std::string f = set.recent[size_t(sel - recentStart)];
                confirmDiscard([this, f]() { openDoc(f); });
            } else if (it.rfind("Quit", 0) == 0) quit();
            break;
        }
        case M_EDIT:
            switch (sel) {
            case 0: undo(); break;
            case 1: redo(); break;
            case 3: cutSelection(); break;
            case 4: copySelection(); break;
            case 5: paste(); break;
            case 6: duplicateSelection(); break;
            case 7: deleteSelection(); break;
            case 9: if (t && primaryObj() > 0) selectObj(parentOf(*t, primaryObj())); break;
            case 10:
                if (t && primaryObj() >= 0) {
                    std::vector<int> k = childrenOf(*t, primaryObj());
                    if (!k.empty()) selObjs = k;
                }
                break;
            }
            break;
        case M_RES:
            if (sel <= 4) addItem(sel);
            else if (sel == 6) duplicateItem();
            else if (sel == 7) deleteItem();
            else if (sel == 8) moveItem(-1);
            else if (sel == 9) moveItem(1);
            else if (sel == 11) sortCurrentTree();
            else if (sel == 12) autoName();
            else if (sel == 13) {
                std::vector<std::string> p = checkNames(doc);
                std::string s;
                for (size_t k = 0; k < p.size() && k < 12; k++) s += p[k] + "\n";
                message("Check names", p.empty() ? "All names are valid and unique." : s);
            }
            break;
        case M_OBJ:
            switch (sel) {
            case 0: reorderSelection(1); break;
            case 1: reorderSelection(-1); break;
            case 2: reorderSelection(2); break;
            case 3: reorderSelection(-2); break;
            case 5: moveIntoPrevSibling(); break;
            case 6: moveOutToParent(); break;
            case 8: alignSelection(0); break;
            case 9: alignSelection(1); break;
            case 10: alignSelection(2); break;
            case 11: alignSelection(3); break;
            case 12: alignSelection(4); break;
            case 13: alignSelection(5); break;
            case 14: alignSelection(6); break;
            case 15: fitParent(); break;
            case 17: editBitmap(); break;
            case 19: menuCommand(0); break;
            case 20: menuCommand(1); break;
            case 21: menuCommand(2); break;
            case 22: menuCommand(4); break;
            case 23: menuCommand(3); break;
            }
            break;
        case M_VIEW:
            if (sel == 0) set.zoom = std::min(8, set.zoom + 1);
            else if (sel == 1) set.zoom = std::max(1, set.zoom - 1);
            else if (sel == 3) set.grid = !set.grid;
            else if (sel == 4) set.snap = !set.snap;
            else if (sel == 5) set.outlines = !set.outlines;
            else if (sel == 7) enterTestMode(!testMode);
            break;
        case M_HELP:
            if (sel == 0) pushModal(makeShortcutsDialog());
            else pushModal(makeAboutDialog());
            break;
        }
        redraw_ = true;
    }
}

// ================================================================= toolbar
void App::drawToolbar(const Rect& r)
{
    ui::Canvas& c = canvas;
    c.fill(r, col::panel);
    c.hline(r.x, r.x + r.w - 1, r.y + r.h - 1, col::line);
    int x = 8, y = r.y + 4, h = 22;
    std::vector<std::string> aes, res;
    for (auto& a : gem::aesProfiles()) aes.push_back(a.name);
    for (auto& rr : gem::resolutions()) res.push_back(rr.name);
    int a = set.aes, rs = set.res;
    c.text(x, y + 3, "AES", col::text);
    x += 32;
    if (u.dropdown(u.id("tb.aes"), Rect(x, y, 260, h), a, aes)) setMode(a, set.res);
    x += 268;
    c.text(x, y + 3, "Screen", col::text);
    x += 56;
    if (u.dropdown(u.id("tb.res"), Rect(x, y, 230, h), rs, res)) setMode(set.aes, rs);
    x += 240;
    c.text(x, y + 3, "Zoom", col::text);
    x += 40;
    if (u.button(Rect(x, y, 22, h), "-", set.zoom > 1)) set.zoom--;
    c.textCentered(Rect(x + 22, y, 26, h), std::to_string(set.zoom) + "x", col::text);
    if (u.button(Rect(x + 48, y, 22, h), "+", set.zoom < 8)) set.zoom++;
    if (u.button(Rect(x + 74, y, 36, h), "Fit")) fitPending = true;
    x += 118;
    if (u.button(Rect(x, y, 48, h), "Grid", true, set.grid)) set.grid = !set.grid;
    x += 52;
    if (u.button(Rect(x, y, 48, h), "Snap", true, set.snap)) set.snap = !set.snap;
    x += 52;
    if (u.button(Rect(x, y, 72, h), "Outlines", true, set.outlines)) set.outlines = !set.outlines;
    x += 76;
    if (u.button(Rect(x, y, 56, h), "Test", selKind == ItemKind::Tree, testMode)) enterTestMode(!testMode);
    x += 64;

    Tree* t = curTree();
    MenuInfo mi;
    if (t && t->kind == TreeKind::Menu && analyseMenu(*t, mi) && x + 300 < r.w) {
        c.text(x, y + 3, "Dropdown", col::text);
        x += 72;
        std::vector<std::string> titles = { "(none)" };
        for (int ti : mi.titles) titles.push_back(t->obj[ti].str);
        int s = openMenuTitle + 1;
        if (u.dropdown(u.id("tb.menu"), Rect(x, y, 160, h), s, titles)) openMenuTitle = s - 1;
    } else if (selKind == ItemKind::String && selItem >= 0 && selItem < (int)doc.strings.size() &&
               doc.strings[selItem].isAlert() && x + 220 < r.w) {
        c.text(x, y + 3, "Default button", col::text);
        x += 120;
        u.intField(u.id("tb.alertdef"), Rect(x, y, 80, h), alertDefault, 0, 3);
    }
}

// ================================================================= resource list
void App::drawResourcePanel(const Rect& r)
{
    ui::Canvas& c = canvas;
    c.fill(r, col::panel);
    c.vline(r.x + r.w - 1, r.y, r.y + r.h, col::line);
    u.header(Rect(r.x, r.y, r.w - 1, 20), "RESOURCE  " + std::to_string(doc.trees.size()) + " trees, " +
                                              std::to_string(doc.strings.size()) + " str, " +
                                              std::to_string(doc.images.size()) + " img");
    int btnH = 2 * 24 + 6;
    Rect list(r.x + 4, r.y + 24, r.w - 22, r.h - 24 - btnH);
    c.fill(list, col::field);
    c.frame(list, col::line);

    struct Row { ItemKind k; int i; };
    std::vector<Row> rows;
    for (size_t i = 0; i < doc.trees.size(); i++) rows.push_back({ ItemKind::Tree, int(i) });
    for (size_t i = 0; i < doc.strings.size(); i++) rows.push_back({ ItemKind::String, int(i) });
    for (size_t i = 0; i < doc.images.size(); i++) rows.push_back({ ItemKind::Image, int(i) });
    int content = (int)rows.size() * ROW_H;
    if (u.hover(list) && u.in.wheelY != 0) { resScroll += int(u.in.wheelY * ROW_H * 3); u.in.wheelY = 0; }
    u.scrollbar(u.id("res.sb"), Rect(list.x + list.w, list.y, 13, list.h), content, list.h - 4, resScroll);
    c.pushClip(list.inset(1));
    for (size_t k = 0; k < rows.size(); k++) {
        Rect rr(list.x + 1, list.y + 2 + int(k) * ROW_H - resScroll, list.w - 2, ROW_H);
        if (rr.y + rr.h < list.y || rr.y > list.y + list.h) continue;
        bool sel = rows[k].k == selKind && rows[k].i == selItem;
        if (sel) c.fill(rr, col::selBg);
        else if (u.hover(rr)) c.fill(rr, col::hover);
        std::string kind;
        if (rows[k].k == ItemKind::Tree) kind = doc.trees[rows[k].i].kind == TreeKind::Menu ? "menu" : "tree";
        else if (rows[k].k == ItemKind::String) kind = doc.strings[rows[k].i].isAlert() ? "alert" : "string";
        else kind = "image";
        c.text(rr.x + 4, rr.y + 1, itemLabel(doc, rows[k].k, rows[k].i), sel ? col::selText : col::text, rr.w - 70);
        c.text(rr.x + rr.w - 8 * (int)kind.size() - 4, rr.y + 1, kind, sel ? col::selText : col::dim);
        if (u.clicked(rr)) selectItem(rows[k].k, rows[k].i);
    }
    c.popClip();

    int by = r.y + r.h - btnH + 2, bw = (r.w - 10) / 5;
    const char* add[] = { "+Tree", "+Menu", "+Alert", "+Str", "+Img" };
    for (int i = 0; i < 5; i++)
        if (u.button(Rect(r.x + 4 + i * bw, by, bw - 2, 22), add[i])) addItem(i);
    by += 24;
    bw = (r.w - 10) / 4;
    if (u.button(Rect(r.x + 4, by, bw - 2, 22), "Up", selItem > 0)) moveItem(-1);
    if (u.button(Rect(r.x + 4 + bw, by, bw - 2, 22), "Down", selItem >= 0)) moveItem(1);
    if (u.button(Rect(r.x + 4 + 2 * bw, by, bw - 2, 22), "Dup", selItem >= 0)) duplicateItem();
    if (u.button(Rect(r.x + 4 + 3 * bw, by, bw - 2, 22), "Delete", selItem >= 0))
        ask("Delete item", "Delete \"" + itemLabel(doc, selKind, selItem).substr(2) + "\"?", { "Delete", "Cancel" },
            [this](int b) { if (b == 0) deleteItem(); });
}

// ================================================================= object outline
void App::drawOutlinePanel(const Rect& r)
{
    ui::Canvas& c = canvas;
    c.fill(r, col::panel);
    c.vline(r.x + r.w - 1, r.y, r.y + r.h, col::line);
    Tree* t = curTree();
    u.header(Rect(r.x, r.y, r.w - 1, 20), t ? "OBJECTS  " + std::to_string(t->obj.size()) : "OBJECTS");
    int btnH = 28;
    Rect list(r.x + 4, r.y + 24, r.w - 22, r.h - 24 - btnH);
    c.fill(list, col::field);
    c.frame(list, col::line);
    if (!t) {
        c.text(list.x + 6, list.y + 6, selKind == ItemKind::String ? "(free string)" : "(free image)", col::dim);
        return;
    }
    // depth-first order as drawn by objc_draw
    std::vector<std::pair<int, int>> rows;
    std::function<void(int, int)> walk = [&](int o, int d) {
        rows.push_back({ o, d });
        for (int k : childrenOf(*t, o)) walk(k, d + 1);
    };
    if (!t->obj.empty()) walk(0, 0);
    int content = (int)rows.size() * ROW_H;
    if (u.hover(list) && u.in.wheelY != 0) { outlineScroll += int(u.in.wheelY * ROW_H * 3); u.in.wheelY = 0; }
    // keep primary selection visible when it changed
    static int lastPrimary = -2;
    if (primaryObj() != lastPrimary) {
        lastPrimary = primaryObj();
        for (size_t k = 0; k < rows.size(); k++)
            if (rows[k].first == lastPrimary) {
                int y = int(k) * ROW_H;
                if (y < outlineScroll) outlineScroll = y;
                if (y + ROW_H > outlineScroll + list.h - 4) outlineScroll = y + ROW_H - list.h + 4;
            }
    }
    u.scrollbar(u.id("obj.sb"), Rect(list.x + list.w, list.y, 13, list.h), content, list.h - 4, outlineScroll);
    c.pushClip(list.inset(1));
    for (size_t k = 0; k < rows.size(); k++) {
        int o = rows[k].first, d = rows[k].second;
        Rect rr(list.x + 1, list.y + 2 + int(k) * ROW_H - outlineScroll, list.w - 2, ROW_H);
        if (rr.y + rr.h < list.y || rr.y > list.y + list.h) continue;
        bool sel = isSelected(o);
        bool hid = t->obj[o].flags & HIDETREE;
        if (sel) c.fill(rr, o == primaryObj() ? col::selBg : col::dim);
        else if (u.hover(rr) || o == hoverObj) c.fill(rr, col::hover);
        // tree lines
        for (int i = 0; i < d; i++) c.vline(rr.x + 8 + i * 12, rr.y, rr.y + rr.h - 1, sel ? 0x808080 : col::light);
        c.text(rr.x + 4 + d * 12, rr.y + 1, objectLabel(*t, o), sel ? col::selText : hid ? col::disabled : col::text,
               rr.w - 8 - d * 12);
        if (u.clicked(rr)) {
            selectObj(o, (u.in.mods & (MOD_CTRL | MOD_SHIFT)) != 0);
            if (testMode) enterTestMode(false);
        }
    }
    c.popClip();
    int by = r.y + r.h - btnH + 3, bw = (r.w - 10) / 5;
    bool has = primaryObj() > 0;
    if (u.button(Rect(r.x + 4, by, bw - 2, 22), "Back", has)) reorderSelection(-1);
    if (u.button(Rect(r.x + 4 + bw, by, bw - 2, 22), "Fwd", has)) reorderSelection(1);
    if (u.button(Rect(r.x + 4 + 2 * bw, by, bw - 2, 22), "In", has)) moveIntoPrevSibling();
    if (u.button(Rect(r.x + 4 + 3 * bw, by, bw - 2, 22), "Out", has)) moveOutToParent();
    if (u.button(Rect(r.x + 4 + 4 * bw, by, bw - 2, 22), "Del", has)) deleteSelection();
}

// ================================================================= palette
void App::drawPalette(const Rect& r)
{
    ui::Canvas& c = canvas;
    c.fill(r, col::panel);
    c.vline(r.x, r.y, r.y + r.h, col::line);
    u.header(Rect(r.x, r.y, r.w, 20), "ADD OBJECT (child of selection)");
    static const int types[] = { G_BOX, G_IBOX, G_BOXCHAR, G_STRING, G_BUTTON, G_TITLE, G_TEXT, G_BOXTEXT,
                                 G_FTEXT, G_FBOXTEXT, G_IMAGE, G_ICON, G_CICON, G_USERDEF };
    int bw = (r.w - 12) / 3;
    for (int i = 0; i < 14; i++) {
        Rect b(r.x + 6 + (i % 3) * bw, r.y + 24 + (i / 3) * 24, bw - 3, 22);
        bool ok = !testMode;
        if (types[i] == G_CICON && !env.aes.colourIcons()) ok = ok && true;  // allowed, drawn only by AES >= 3.30
        if (u.button(b, shortTypeName(types[i]), ok)) addObject(types[i]);
    }
}

// ================================================================= status
void App::drawStatus(const Rect& r)
{
    ui::Canvas& c = canvas;
    c.fill(r, col::panel);
    c.hline(r.x, r.x + r.w - 1, r.y, col::line);
    std::string right;
    Tree* t = curTree();
    if (t && primaryObj() >= 0 && primaryObj() < (int)t->obj.size()) {
        const Object& o = t->obj[primaryObj()];
        char buf[160];
        snprintf(buf, sizeof buf, "#%d %s  x%d y%d w%d h%d px", primaryObj(), shortTypeName(o.baseType()),
                 toPixels(o.x, cellW()), toPixels(o.y, cellH()), toPixels(o.w, cellW(), true, env.res.w),
                 toPixels(o.h, cellH()));
        right = buf;
    }
    if (viewRect.contains(u.in.mx, u.in.my)) {
        int ax = (u.in.mx - viewOx) / std::max(1, viewZx), ay = (u.in.my - viewOy) / std::max(1, viewZy);
        if (ax >= 0 && ay >= 0 && ax < env.res.w && ay < env.res.h)
            right += "   mouse " + std::to_string(ax) + "," + std::to_string(ay);
    }
    int rw = ui::Canvas::textW(right);
    c.text(r.x + 6, r.y + 3, (testMode ? "[TEST] " : "") + status, testMode ? col::accent : col::text, r.w - rw - 24);
    c.text(r.x + r.w - rw - 8, r.y + 3, right, col::dim);
}

// ================================================================= shortcuts
void App::handleShortcuts()
{
    Tree* t = curTree();
    for (auto& k : u.in.keys) {
        bool ctrl = k.mods & MOD_CTRL, shift = k.mods & MOD_SHIFT;
        // while a text field has focus only Ctrl shortcuts reach the app
        if (u.wantsKeyboard() && !ctrl) continue;
        if (u.wantsKeyboard() && (k.sym == 'c' || k.sym == 'x' || k.sym == 'v' || k.sym == 'a')) continue;
        if (testMode && !ctrl && k.sym != K_F5 && k.sym != K_F1) { testKey(k); continue; }
        if (ctrl) {
            switch (k.sym) {
            case 'n': confirmDiscard([this]() { newDoc(); }); break;
            case 'o': cmdOpen(); break;
            case 's': if (shift) cmdSaveAs(); else cmdSave(); break;
            case 'q': quit(); break;
            case 'z': if (shift) redo(); else undo(); break;
            case 'y': redo(); break;
            case 'c': copySelection(); break;
            case 'x': cutSelection(); break;
            case 'v': paste(); break;
            case 'd': duplicateSelection(); break;
            case 'e': editBitmap(); break;
            case 'f': reorderSelection(1); break;
            case 'b': reorderSelection(-1); break;
            case 'g': set.grid = !set.grid; break;
            case 't': enterTestMode(!testMode); break;
            case 'a':
                if (t && !t->obj.empty()) {
                    int p = primaryObj() >= 0 ? primaryObj() : 0;
                    std::vector<int> kids = childrenOf(*t, p);
                    if (kids.empty() && p > 0) kids = childrenOf(*t, parentOf(*t, p));
                    if (!kids.empty()) selObjs = kids;
                }
                break;
            case '+': case '=': case 0xffab: set.zoom = std::min(8, set.zoom + 1); break;
            case '-': case 0xffad: set.zoom = std::max(1, set.zoom - 1); break;
            case K_LEFT: nudge(-1, 0, true, shift); break;
            case K_RIGHT: nudge(1, 0, true, shift); break;
            case K_UP: nudge(0, -1, true, shift); break;
            case K_DOWN: nudge(0, 1, true, shift); break;
            case K_PAGEUP: moveItem(-1); break;
            case K_PAGEDOWN: moveItem(1); break;
            }
            continue;
        }
        switch (k.sym) {
        case K_DELETE: deleteSelection(); break;
        case K_BACKSPACE: if (t && primaryObj() > 0) selectObj(parentOf(*t, primaryObj())); break;
        case K_LEFT: nudge(-1, 0, false, shift); break;
        case K_RIGHT: nudge(1, 0, false, shift); break;
        case K_UP: nudge(0, -1, false, shift); break;
        case K_DOWN: nudge(0, 1, false, shift); break;
        case K_F5: enterTestMode(!testMode); break;
        case K_F1: pushModal(makeShortcutsDialog()); break;
        case K_ESCAPE:
            if (testMode) enterTestMode(false);
            else selObjs.clear();
            break;
        case K_TAB: case K_ISO_LEFT_TAB:
            if (t && !t->obj.empty()) {
                int n = (int)t->obj.size();
                int p = primaryObj();
                p = (k.sym == K_ISO_LEFT_TAB || shift) ? (p <= 0 ? n - 1 : p - 1) : (p + 1) % n;
                selectObj(p);
            }
            break;
        case K_PAGEUP: case K_PAGEDOWN: {
            // previous / next resource item
            struct R { ItemKind k; int i; };
            std::vector<R> all;
            for (size_t i = 0; i < doc.trees.size(); i++) all.push_back({ ItemKind::Tree, int(i) });
            for (size_t i = 0; i < doc.strings.size(); i++) all.push_back({ ItemKind::String, int(i) });
            for (size_t i = 0; i < doc.images.size(); i++) all.push_back({ ItemKind::Image, int(i) });
            int cur = 0;
            for (size_t i = 0; i < all.size(); i++) if (all[i].k == selKind && all[i].i == selItem) cur = int(i);
            cur += k.sym == K_PAGEUP ? -1 : 1;
            if (cur >= 0 && cur < (int)all.size()) selectItem(all[cur].k, all[cur].i);
            break;
        }
        }
    }
    u.in.keys.clear();
}

} // namespace app
