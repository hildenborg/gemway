#include "app/app.h"
#include "base/bytes.h"
#include <algorithm>
#include <cctype>

namespace app {

using namespace rsc;
namespace col = ui::col;

ui::Id App::fieldId(const char* name) const
{
    uint64_t seed = 0x51ED27ull * uint64_t(int(selKind) + 1) + 0x9E37ull * uint64_t(selItem + 2) +
                    0x7F4Aull * uint64_t(primaryObj() + 3);
    return ui::Ui::hash(name, seed * 1099511628211ull + 1);
}

namespace {

const uint16_t kPat[8][4] = {
    { 0x0000, 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x4444, 0x0000, 0x1111 }, { 0x0000, 0x5555, 0x0000, 0x5555 },
    { 0x8888, 0x5555, 0x2222, 0x5555 }, { 0xAAAA, 0x5555, 0xAAAA, 0x5555 }, { 0xAAAA, 0xDDDD, 0xAAAA, 0x7777 },
    { 0xAAAA, 0xFFFF, 0xAAAA, 0xFFFF }, { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF },
};

struct Form {
    App& a;
    ui::Ui& u;
    ui::Canvas& c;
    int x, y, w, lw = 100;

    Rect field(const std::string& label, int h = 20)
    {
        c.text(x, y + (h - 16) / 2, label, col::dim, lw - 4);
        Rect f(x + lw, y, w - lw, h);
        y += h + 4;
        return f;
    }
    Rect full(int h = 20)
    {
        Rect f(x, y, w, h);
        y += h + 4;
        return f;
    }
    void section(const std::string& t)
    {
        y += 6;
        c.text(x, y, t, col::text);
        c.hline(x + ui::Canvas::textW(t) + 6, x + w - 1, y + 8, col::light);
        y += 20;
    }
    void note(const std::string& t, uint32_t color = col::dim)
    {
        c.text(x, y, t, color, w);
        y += 18;
    }
};

std::string sanitizeName(const std::string& s)
{
    std::string o;
    for (char ch : s)
        if (isalnum((unsigned char)ch) || ch == '_') o += ch;
    if (o.size() > 16) o.resize(16);
    return o;
}

// colour index dropdown with palette swatches of the current screen mode
bool colourDropdown(App& a, ui::Id id, const Rect& r, int& v)
{
    std::vector<std::string> names = colourNames(16);
    const gem::Screen& s = a.screen;
    ui::Canvas& c = a.canvas;
    return a.u.dropdown(id, r, v, names, true, [&c, &s](int i, const Rect& rr) {
        drawSwatch(c, Rect(rr.x, rr.y + 1, 18, std::max(4, rr.h - 2)), s.pal[s.pen(i)]);
    });
}

bool patternDropdown(App& a, ui::Id id, const Rect& r, int& v)
{
    static const std::vector<std::string> names = { "0 Hollow", "1 Pattern", "2 Pattern", "3 Pattern",
                                                    "4 Pattern", "5 Pattern", "6 Pattern", "7 Solid" };
    ui::Canvas& c = a.canvas;
    return a.u.dropdown(id, r, v, names, true, [&c](int i, const Rect& rr) {
        Rect sw(rr.x, rr.y + 1, 18, std::max(4, rr.h - 2));
        c.fill(sw, 0xFFFFFF);
        for (int yy = 0; yy < sw.h; yy++)
            for (int xx = 0; xx < sw.w; xx++)
                if (kPat[i][(sw.y + yy) & 3] & (0x8000 >> ((sw.x + xx) & 15))) c.fill(Rect(sw.x + xx, sw.y + yy, 1, 1), 0);
        c.frame(sw, col::line);
    });
}

// edit an AES colour word: border(15-12) text(11-8) mode(7) pattern(6-4) interior(3-0)
bool colourWord(Form& f, App& a, const char* key, uint16_t& cw, bool border, bool text)
{
    bool ch = false;
    int bc = (cw >> 12) & 15, tc = (cw >> 8) & 15, md = (cw >> 7) & 1, ip = (cw >> 4) & 7, ic = cw & 15;
    std::string k = key;
    if (border && colourDropdown(a, a.fieldId((k + ".bc").c_str()), f.field("Border col"), bc)) ch = true;
    if (text && colourDropdown(a, a.fieldId((k + ".tc").c_str()), f.field("Text col"), tc)) ch = true;
    if (text) {
        static const std::vector<std::string> modes = { "Transparent", "Replace" };
        if (a.u.dropdown(a.fieldId((k + ".md").c_str()), f.field("Text mode"), md, modes)) ch = true;
    }
    if (patternDropdown(a, a.fieldId((k + ".ip").c_str()), f.field("Fill"), ip)) ch = true;
    if (colourDropdown(a, a.fieldId((k + ".ic").c_str()), f.field("Fill col"), ic)) ch = true;
    if (ch) cw = uint16_t((bc << 12) | (tc << 8) | (md << 7) | (ip << 4) | ic);
    return ch;
}

std::string escAlert(const std::string& s)
{
    std::string o;
    for (char ch : s) {
        o += ch;
        if (ch == '|' || ch == ']') o += ch;
    }
    return o;
}

std::string buildAlert(int icon, const std::vector<std::string>& lines, const std::vector<std::string>& buttons)
{
    std::string s = "[" + std::to_string(icon) + "][";
    int nl = 0;
    for (int i = (int)lines.size() - 1; i >= 0; i--) if (!lines[i].empty()) { nl = i + 1; break; }
    nl = std::max(nl, 1);
    for (int i = 0; i < nl; i++) s += (i ? "|" : "") + escAlert(lines[i]);
    s += "][";
    int nb = 0;
    for (int i = (int)buttons.size() - 1; i >= 0; i--) if (!buttons[i].empty()) { nb = i + 1; break; }
    nb = std::max(nb, 1);
    for (int i = 0; i < nb; i++) s += (i ? "|" : "") + escAlert(buttons[i]);
    return s + "]";
}

// convert an object to another type keeping geometry, flags, name and text
void changeType(Object& o, int newType, int cw, int ch)
{
    std::string text;
    if (typeHasString(o.baseType())) text = o.str;
    else if (typeHasTedinfo(o.baseType())) text = o.ted.text.empty() ? o.ted.tmplt : o.ted.text;
    else if (o.baseType() == G_ICON || o.baseType() == G_CICON) text = o.icon.text;
    Object n = makeObject(newType, cw, ch);
    n.next = o.next; n.head = o.head; n.tail = o.tail;
    n.x = o.x; n.y = o.y; n.w = o.w; n.h = o.h;
    n.flags = o.flags; n.state = o.state; n.name = o.name;
    n.type = uint16_t((o.type & 0xFF00) | newType);
    if (!text.empty()) {
        if (typeHasString(newType)) n.str = text;
        else if (newType == G_TEXT || newType == G_BOXTEXT) n.ted.text = text;
        else if (newType == G_ICON || newType == G_CICON) n.icon.text = text.substr(0, 12);
    }
    if ((o.baseType() == G_ICON && newType == G_CICON) || (o.baseType() == G_CICON && newType == G_ICON)) {
        n.icon = o.icon;
        if (newType == G_ICON) n.cicons.clear();
    }
    if ((o.baseType() == G_BOX || o.baseType() == G_IBOX || o.baseType() == G_BOXCHAR) && typeHasBoxSpec(newType))
        n.spec = (o.spec & 0x00FFFFFF) | (newType == G_BOXCHAR ? (o.spec & 0xFF000000) : 0);
    if (typeHasTedinfo(o.baseType()) && typeHasTedinfo(newType)) n.ted = o.ted;
    o = n;
}

const int kTypes[] = { G_BOX, G_TEXT, G_BOXTEXT, G_IMAGE, G_USERDEF, G_IBOX, G_BUTTON, G_BOXCHAR, G_STRING,
                       G_FTEXT, G_FBOXTEXT, G_ICON, G_TITLE, G_CICON, G_SWBUTTON, G_POPUP, G_WINTITLE, G_EDIT, G_SHORTCUT };

} // namespace

int App::drawInspector(const Rect& r, int scroll)
{
    Form f{ *this, u, canvas, r.x, r.y, r.w };
    ui::Canvas& c = canvas;

    // ------------------------------------------------------------ free strings / alerts
    if (selKind == ItemKind::String && selItem >= 0 && selItem < (int)doc.strings.size()) {
        FreeString& fs = doc.strings[selItem];
        f.section(fs.isAlert() ? "Alert (free string)" : "Free string");
        std::string nm = fs.name;
        if (u.textField(fieldId("name"), f.field("Name"), nm, 16)) { checkpoint(fieldId("name")); fs.name = sanitizeName(nm); }
        if (fs.isAlert()) {
            gem::AlertInfo ai = gem::Aes::parseAlert(fs.text);
            std::vector<std::string> lines = ai.lines, buttons = ai.buttons;
            lines.resize(5);
            buttons.resize(3);
            int icon = std::clamp(ai.icon, 0, 3);
            bool ch = false;
            static const std::vector<std::string> icons = { "0 None", "1 Note (!)", "2 Question (?)", "3 Stop" };
            if (u.dropdown(fieldId("al.icon"), f.field("Icon"), icon, icons)) ch = true;
            for (int i = 0; i < 5; i++) {
                std::string l = lines[i];
                ui::Id id = fieldId(("al.l" + std::to_string(i)).c_str());
                if (u.textField(id, f.field("Line " + std::to_string(i + 1)), l, 40)) { lines[i] = l; ch = true; checkpoint(id); }
            }
            for (int i = 0; i < 3; i++) {
                std::string b = buttons[i];
                ui::Id id = fieldId(("al.b" + std::to_string(i)).c_str());
                if (u.textField(id, f.field("Button " + std::to_string(i + 1)), b, 20)) { buttons[i] = b; ch = true; checkpoint(id); }
            }
            if (ch) {
                if (u.focus == 0) checkpoint();
                fs.text = buildAlert(icon, lines, buttons);
            }
            for (auto& l : ai.lines)
                if (l.size() > 32) { f.note("Line longer than 32 characters (TOS limit)", col::warn); break; }
            for (auto& b : ai.buttons)
                if (b.size() > 10) { f.note("Button longer than 10 characters (TOS limit)", col::warn); break; }
            if (ai.tooManyLines) f.note("More than 5 lines: extra lines are ignored", col::warn);
            if (ai.tooManyButtons) f.note("More than 3 buttons: extra buttons are ignored", col::warn);
            f.note("Default button for preview: toolbar");
            f.section("Raw alert string");
        }
        std::string t = fs.text;
        if (u.textField(fieldId("text"), f.full(), t, 255)) { checkpoint(fieldId("text")); fs.text = t; }
        f.note(std::to_string(fs.text.size()) + " characters");
        if (!fs.isAlert() && u.button(f.full(22), "Convert to alert")) {
            checkpoint();
            fs.text = "[1][" + escAlert(fs.text) + "][ OK ]";
        }
        return f.y - r.y;
    }

    // ------------------------------------------------------------ free images
    if (selKind == ItemKind::Image && selItem >= 0 && selItem < (int)doc.images.size()) {
        FreeImage& im = doc.images[selItem];
        f.section("Free image (BITBLK)");
        std::string nm = im.name;
        if (u.textField(fieldId("name"), f.field("Name"), nm, 16)) { checkpoint(fieldId("name")); im.name = sanitizeName(nm); }
        f.note("Size " + std::to_string(im.bit.img.w) + " x " + std::to_string(im.bit.img.h) + " pixels");
        int colr = im.bit.color;
        if (colourDropdown(*this, fieldId("color"), f.field("Colour"), colr)) { checkpoint(); im.bit.color = int16_t(colr); }
        if (u.button(f.full(22), "Edit image...  (Ctrl+E)")) editBitmap();
        return f.y - r.y;
    }

    Tree* t = curTree();
    if (!t) {
        f.note("Nothing selected.");
        return f.y - r.y;
    }

    // ------------------------------------------------------------ menu helpers
    MenuInfo mi;
    bool isMenu = t->kind == TreeKind::Menu && analyseMenu(*t, mi);
    if (isMenu) {
        f.section("Menu");
        int bw = (f.w - 8) / 3;
        if (u.button(Rect(f.x, f.y, bw, 22), "+ Title")) menuCommand(0);
        if (u.button(Rect(f.x + bw + 4, f.y, bw, 22), "+ Item")) menuCommand(1);
        if (u.button(Rect(f.x + 2 * bw + 8, f.y, bw, 22), "+ Separator")) menuCommand(2);
        f.y += 26;
        if (u.button(Rect(f.x, f.y, bw * 3 / 2, 22), "Delete title")) menuCommand(4);
        if (u.button(Rect(f.x + bw * 3 / 2 + 4, f.y, f.w - bw * 3 / 2 - 4, 22), "Recalculate layout")) menuCommand(3);
        f.y += 26;
    }

    int o = primaryObj();
    if (o < 0 || o >= (int)t->obj.size()) {
        // tree properties
        f.section("Tree #" + std::to_string(selItem));
        std::string nm = t->name;
        if (u.textField(fieldId("name"), f.field("Name"), nm, 16)) { checkpoint(fieldId("name")); t->name = sanitizeName(nm); }
        static const std::vector<std::string> kinds = { "Unknown", "Dialog / form", "Menu", "Panel" };
        int k = int(t->kind);
        if (u.dropdown(fieldId("kind"), f.field("Kind"), k, kinds)) { checkpoint(); t->kind = TreeKind(k); }
        f.note(std::to_string(t->obj.size()) + " objects");
        std::string v = validateTree(*t);
        if (!v.empty()) f.note("Problem: " + v, col::warn);
        if (u.button(f.full(22), "Select root object")) selectObj(0);
        if (u.button(f.full(22), "Auto-name interactive objects")) autoName();
        if (u.button(f.full(22), "Renumber objects (tree order)")) sortCurrentTree();
        f.y += 8;
        f.note("Click an object in the view or in the");
        f.note("object list to edit its properties.");
        return f.y - r.y;
    }

    Object& ob = t->obj[o];
    int bt = ob.baseType();
    f.section("Object #" + std::to_string(o) + (selObjs.size() > 1 ? "  (" + std::to_string(selObjs.size()) + " selected)" : ""));

    std::string nm = ob.name;
    if (u.textField(fieldId("name"), f.field("Name"), nm, 16)) { checkpoint(fieldId("name")); ob.name = sanitizeName(nm); }

    std::vector<std::string> typeNames;
    int tsel = 0;
    for (size_t i = 0; i < std::size(kTypes); i++) {
        typeNames.push_back(typeName(kTypes[i]));
        if (kTypes[i] == bt) tsel = int(i);
    }
    if (u.dropdown(fieldId("type"), f.field("Type"), tsel, typeNames)) {
        checkpoint();
        changeType(ob, kTypes[tsel], cellW(), cellH());
        bt = ob.baseType();
    }
    int ext = ob.extType();
    if (u.intField(fieldId("ext"), f.field("Ext. type"), ext, 0, 255)) {
        checkpoint(fieldId("ext"));
        ob.type = uint16_t((ext << 8) | bt);
    }

    // ---- geometry
    f.section("Position & size  (chars + pixels)");
    const char* gl[4] = { "X", "Y", "Width", "Height" };
    int16_t* gv[4] = { &ob.x, &ob.y, &ob.w, &ob.h };
    for (int i = 0; i < 4; i++) {
        Rect row = f.field(gl[i]);
        int chs = rawChars(*gv[i]), px = rawPixOff(*gv[i]);
        int cw = (i & 1) ? cellH() : cellW();
        ui::Id ic = fieldId((std::string("g.c") + char('0' + i)).c_str());
        ui::Id ip = fieldId((std::string("g.p") + char('0' + i)).c_str());
        int fw = (row.w - 70) / 2;
        bool chg = false;
        if (u.intField(ic, Rect(row.x, row.y, fw, row.h), chs, 0, 255)) chg = true;
        c.text(row.x + fw + 2, row.y + 2, "+", col::dim);
        if (u.intField(ip, Rect(row.x + fw + 12, row.y, fw, row.h), px, -127, 127)) chg = true;
        int total = toPixels(makeRaw(chs, px), cw, i == 2, env.res.w);
        c.text(row.x + 2 * fw + 18, row.y + 2, "=" + std::to_string(total), col::dim);
        if (chg) {
            checkpoint(ic);
            *gv[i] = makeRaw(chs, px);
        }
    }
    if (o == 0 && t->kind != TreeKind::Menu) f.note("Root position is set by form_center()");
    if (ob.baseType() != G_IBOX || true) {
        int bw = (f.w - 4) / 2;
        if (u.button(Rect(f.x, f.y, bw, 20), "Snap to grid")) alignSelection(6);
        if (u.button(Rect(f.x + bw + 4, f.y, bw, 20), "Fit to children", !childrenOf(*t, o).empty())) fitParent();
        f.y += 24;
    }

    // ---- flags
    f.section("Flags (ob_flags)");
    auto flagBox = [&](int col, const char* label, uint16_t bit, bool isState) {
        uint16_t cur = isState ? ob.state : ob.flags;
        bool v = cur & bit;
        Rect rr(f.x + col * (f.w / 2), f.y, f.w / 2 - 4, 18);
        bool en = !(bit == LASTOB && !isState);
        if (u.checkbox(rr, label, v, en)) {
            checkpoint();
            for (int s : selObjs) {
                if (s < 0 || s >= (int)t->obj.size()) continue;
                uint16_t& tgt = isState ? t->obj[s].state : t->obj[s].flags;
                if (v) tgt |= bit; else tgt &= uint16_t(~bit);
                // default and radio semantics helpers
                if (!isState && bit == DEFAULT && v) {
                    for (size_t k = 0; k < t->obj.size(); k++)
                        if (int(k) != s) t->obj[k].flags &= ~DEFAULT;
                }
            }
        }
    };
    struct FB { const char* n; uint16_t b; };
    static const FB flags[] = { { "SELECTABLE", SELECTABLE }, { "DEFAULT", DEFAULT }, { "EXIT", EXIT },
                                { "EDITABLE", EDITABLE }, { "RBUTTON", RBUTTON }, { "TOUCHEXIT", TOUCHEXIT },
                                { "HIDETREE", HIDETREE }, { "INDIRECT", INDIRECT }, { "SUBMENU", SUBMENU },
                                { "LASTOB", LASTOB }, { "FLAG12", 0x1000 }, { "FLAG13", 0x2000 },
                                { "FLAG14", 0x4000 }, { "FLAG15", 0x8000 } };
    for (size_t i = 0; i < std::size(flags); i++) {
        flagBox(int(i % 2), flags[i].n, flags[i].b, false);
        if (i % 2) f.y += 20;
    }
    f.y += 4;
    static const std::vector<std::string> d3 = { "None", "Indicator (FL3DIND)", "Background (FL3DBAK)", "Activator (FL3DACT)" };
    int d3sel = (ob.flags & FL3DMASK) >> 9;
    if (u.dropdown(fieldId("3d"), f.field("3D effect"), d3sel, d3)) {
        checkpoint();
        for (int s : selObjs)
            if (s >= 0 && s < (int)t->obj.size()) t->obj[s].flags = uint16_t((t->obj[s].flags & ~FL3DMASK) | (d3sel << 9));
    }
    if ((ob.flags & FL3DMASK) && !env.use3D())
        f.note(env.aes.threeD() ? "3D needs 16+ colours in this mode" : "3D flags are ignored by this AES");

    // ---- state
    f.section("State (ob_state)");
    static const FB states[] = { { "SELECTED", SELECTED }, { "CROSSED", CROSSED }, { "CHECKED", CHECKED },
                                 { "DISABLED", DISABLED }, { "OUTLINED", OUTLINED }, { "SHADOWED", SHADOWED },
                                 { "WHITEBAK", WHITEBAK }, { "DRAW3D", DRAW3D } };
    for (size_t i = 0; i < std::size(states); i++) {
        flagBox(int(i % 2), states[i].n, states[i].b, true);
        if (i % 2) f.y += 20;
    }
    uint32_t hi = ob.state >> 8;
    if (u.hexField(fieldId("st.hi"), f.field("State 8-15"), hi, 2)) {
        checkpoint(fieldId("st.hi"));
        ob.state = uint16_t((ob.state & 0xFF) | ((hi & 0xFF) << 8));
    }

    // ---- type specific
    switch (bt) {
    case G_STRING: case G_BUTTON: case G_TITLE: case G_SHORTCUT: case G_WINTITLE: {
        f.section("Text");
        std::string s = ob.str;
        if (u.textField(fieldId("text"), f.full(), s, 255)) {
            checkpoint(fieldId("text"));
            ob.str = s;
        }
        int need = (int)ob.str.size();
        if (need > rawChars(ob.w) && rawPixOff(ob.w) == 0 && bt != G_BUTTON) {
            if (u.button(f.full(20), "Fit width to text (" + std::to_string(need) + " chars)")) {
                checkpoint();
                ob.w = makeRaw(need, 0);
            }
        }
        break;
    }
    case G_BOX: case G_IBOX: case G_BOXCHAR: {
        f.section(bt == G_BOXCHAR ? "Box with character" : bt == G_IBOX ? "Invisible box" : "Box");
        if (bt == G_BOXCHAR) {
            std::string chs(1, char(ob.spec >> 24));
            if (!(ob.spec >> 24)) chs.clear();
            if (u.textField(fieldId("text"), f.field("Character"), chs, 1)) {
                checkpoint(fieldId("text"));
                uint32_t cv = chs.empty() ? 0 : (unsigned char)chs[0];
                ob.spec = (ob.spec & 0x00FFFFFF) | (cv << 24);
            }
            uint32_t code = ob.spec >> 24;
            if (u.hexField(fieldId("chcode"), f.field("Char code"), code, 2)) {
                checkpoint(fieldId("chcode"));
                ob.spec = (ob.spec & 0x00FFFFFF) | ((code & 0xFF) << 24);
            }
        }
        int th = int8_t((ob.spec >> 16) & 0xFF);
        if (u.intField(fieldId("th"), f.field("Border"), th, -128, 127)) {
            checkpoint(fieldId("th"));
            ob.spec = (ob.spec & 0xFF00FFFF) | (uint32_t(uint8_t(int8_t(th))) << 16);
        }
        f.note(th > 0 ? "positive: border drawn inside" : th < 0 ? "negative: border drawn outside" : "no border");
        uint16_t cw = uint16_t(ob.spec);
        if (colourWord(f, *this, "box", cw, true, bt == G_BOXCHAR)) {
            checkpoint();
            ob.spec = (ob.spec & 0xFFFF0000) | cw;
        }
        uint32_t raw = ob.spec;
        if (u.hexField(fieldId("rawspec"), f.field("ob_spec"), raw, 8)) { checkpoint(fieldId("rawspec")); ob.spec = raw; }
        break;
    }
    case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT: {
        TedInfo& te = ob.ted;
        f.section("TEDINFO");
        std::string s = te.text;
        if (u.textField(fieldId("text"), f.field("Text"), s, 255)) { checkpoint(fieldId("text")); te.text = s; }
        if (bt == G_FTEXT || bt == G_FBOXTEXT) {
            s = te.tmplt;
            if (u.textField(fieldId("tmplt"), f.field("Template"), s, 255)) { checkpoint(fieldId("tmplt")); te.tmplt = s; }
            s = te.valid;
            if (u.textField(fieldId("valid"), f.field("Validation"), s, 255)) { checkpoint(fieldId("valid")); te.valid = s; }
            int n = (int)std::count(te.tmplt.begin(), te.tmplt.end(), '_');
            f.note(std::to_string(n) + " input chars. Valid: 9 A a N n F f P p X x");
            if ((int)te.valid.size() < n) f.note("Validation string is shorter than input", col::warn);
            int tl = te.txtlen;
            if (u.intField(fieldId("txtlen"), f.field("te_txtlen"), tl, 0, 255)) { checkpoint(fieldId("txtlen")); te.txtlen = int16_t(tl); }
        } else {
            s = te.tmplt;
            if (u.textField(fieldId("tmplt"), f.field("Template"), s, 255)) { checkpoint(fieldId("tmplt")); te.tmplt = s; }
        }
        static const std::vector<std::string> fonts = { "0 GDOS proportional", "1 GDOS monospaced", "2 GDOS bitmap",
                                                        "3 IBM (system)", "4 ?", "5 SMALL" };
        int fn = std::clamp<int>(te.font, 0, 5);
        if (u.dropdown(fieldId("font"), f.field("Font"), fn, fonts)) { checkpoint(); te.font = int16_t(fn); }
        if (te.font != FONT_IBM && te.font != FONT_SMALL) {
            int fid = te.fontid, fsz = te.fontsize;
            if (u.intField(fieldId("fid"), f.field("Font id"), fid, 0, 32767)) { checkpoint(fieldId("fid")); te.fontid = int16_t(fid); }
            if (u.intField(fieldId("fsz"), f.field("Font size"), fsz, 0, 999)) { checkpoint(fieldId("fsz")); te.fontsize = int16_t(fsz); }
            f.note("GDOS fonts need AES 4.1; shown as system font");
        }
        static const std::vector<std::string> just = { "Left", "Right", "Centre" };
        int j = std::clamp<int>(te.just, 0, 2);
        if (u.dropdown(fieldId("just"), f.field("Justify"), j, just)) { checkpoint(); te.just = int16_t(j); }
        int th = te.thickness;
        if (u.intField(fieldId("th"), f.field("Border"), th, -128, 127)) { checkpoint(fieldId("th")); te.thickness = int16_t(th); }
        uint16_t cw = te.color;
        if (colourWord(f, *this, "ted", cw, bt == G_BOXTEXT || bt == G_FBOXTEXT, true)) { checkpoint(); te.color = cw; }
        break;
    }
    case G_IMAGE: {
        f.section("Image (BITBLK)");
        f.note("Size " + std::to_string(ob.bit.img.w) + " x " + std::to_string(ob.bit.img.h) + " pixels");
        int colr = ob.bit.color;
        if (colourDropdown(*this, fieldId("color"), f.field("Colour"), colr)) { checkpoint(); ob.bit.color = int16_t(colr); }
        int bx = ob.bit.x, by = ob.bit.y;
        if (u.intField(fieldId("bx"), f.field("bi_x"), bx, 0, 4095)) { checkpoint(fieldId("bx")); ob.bit.x = int16_t(bx); }
        if (u.intField(fieldId("by"), f.field("bi_y"), by, 0, 4095)) { checkpoint(fieldId("by")); ob.bit.y = int16_t(by); }
        if (u.button(f.full(22), "Edit image...  (Ctrl+E)")) editBitmap();
        if (u.button(f.full(22), "Size object to image")) {
            checkpoint();
            ob.w = fromPixels(ob.bit.img.w, cellW());
            ob.h = fromPixels(ob.bit.img.h, cellH());
        }
        break;
    }
    case G_ICON: case G_CICON: {
        IconBlk& ib = ob.icon;
        f.section(bt == G_CICON ? "Colour icon (CICONBLK)" : "Icon (ICONBLK)");
        std::string s = ib.text;
        if (u.textField(fieldId("text"), f.field("Text"), s, bt == G_CICON ? 11 : 255)) { checkpoint(fieldId("text")); ib.text = s; }
        std::string chs(1, char(ib.ch & 0xFF));
        if (!(ib.ch & 0xFF)) chs.clear();
        if (u.textField(fieldId("ich"), f.field("Character"), chs, 1)) {
            checkpoint(fieldId("ich"));
            ib.ch = uint16_t((ib.ch & 0xFF00) | (chs.empty() ? 0 : (unsigned char)chs[0]));
        }
        int fg = (ib.ch >> 12) & 15, bg = (ib.ch >> 8) & 15;
        if (colourDropdown(*this, fieldId("ifg"), f.field("Foreground"), fg)) { checkpoint(); ib.ch = uint16_t((ib.ch & 0x0FFF) | (fg << 12)); }
        if (colourDropdown(*this, fieldId("ibg"), f.field("Background"), bg)) { checkpoint(); ib.ch = uint16_t((ib.ch & 0xF0FF) | (bg << 8)); }
        auto quad = [&](const char* label, const char* key, int16_t* v, int n) {
            Rect row = f.field(label);
            int fw = (row.w - (n - 1) * 3) / n;
            for (int i = 0; i < n; i++) {
                int val = v[i];
                ui::Id id = fieldId((std::string(key) + char('0' + i)).c_str());
                if (u.intField(id, Rect(row.x + i * (fw + 3), row.y, fw, row.h), val, -999, 999)) {
                    checkpoint(id);
                    v[i] = int16_t(val);
                }
            }
        };
        quad("Icon x,y", "ixy", &ib.xicon, 2);
        f.note("Icon size " + std::to_string(ib.wicon) + " x " + std::to_string(ib.hicon) + " (change in editor)");
        quad("Text x,y", "txy", &ib.xtext, 2);
        quad("Text w,h", "twh", &ib.wtext, 2);
        quad("Char x,y", "cxy", &ib.xchar, 2);
        if (bt == G_CICON) {
            std::string res;
            for (auto& ci : ob.cicons) res += std::to_string(ci.planes) + (ci.hasSel ? "+sel " : " ");
            f.note("Resolutions (planes): " + (res.empty() ? std::string("none (mono only)") : res));
            if (!env.aes.colourIcons()) f.note("This AES does not support colour icons", col::warn);
        }
        if (u.button(f.full(22), "Edit icon...  (Ctrl+E)")) editBitmap();
        if (u.button(f.full(22), "Size object to icon + text")) {
            checkpoint();
            int x2 = std::max(ib.xicon + ib.wicon, ib.xtext + ib.wtext), y2 = std::max(ib.yicon + ib.hicon, ib.ytext + ib.htext);
            ob.w = fromPixels(x2, cellW());
            ob.h = fromPixels(y2, cellH());
        }
        break;
    }
    case G_USERDEF: default: {
        f.section(bt == G_USERDEF ? "User defined (APPLBLK)" : "Extended object");
        uint32_t raw = ob.spec;
        if (u.hexField(fieldId("rawspec"), f.field("ob_spec"), raw, 8)) { checkpoint(fieldId("rawspec")); ob.spec = raw; }
        f.note(bt == G_USERDEF ? "Drawn by the program at run time" : "Stored unchanged");
        break;
    }
    }
    (void)scroll;
    return f.y - r.y;
}

} // namespace app
