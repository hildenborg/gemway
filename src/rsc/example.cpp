// Builds a demonstration resource that uses every object type, a menu,
// 3D objects, alerts, a colour icon, free strings and a free image.
#include "rsc/rsc.h"
#include <cstdlib>

namespace rsc {

namespace {

Object obj(int type, int x, int y, int w, int h)
{
    Object o = makeObject(type, 8, 16);
    o.x = makeRaw(x, 0); o.y = makeRaw(y, 0); o.w = makeRaw(w, 0); o.h = makeRaw(h, 0);
    return o;
}

Object str(int x, int y, const std::string& s)
{
    Object o = obj(G_STRING, x, y, (int)s.size(), 1);
    o.str = s;
    return o;
}

Object button(int x, int y, int w, const std::string& s, uint16_t flags, const std::string& name)
{
    Object o = obj(G_BUTTON, x, y, w, 1);
    o.str = s; o.flags = flags; o.name = name;
    return o;
}

void drawDisk(Bitmap& data, Bitmap& mask)
{
    data.resize(32, 32);
    mask.resize(32, 32);
    for (int y = 2; y < 30; y++)
        for (int x = 3; x < 29; x++) {
            mask.set(x, y, 1);
            bool edge = x == 3 || x == 28 || y == 2 || y == 29;
            bool shutter = y >= 3 && y <= 11 && x >= 9 && x <= 22 && !(x >= 17 && x <= 19 && y >= 5 && y <= 9);
            bool label = y >= 16 && y <= 27 && x >= 7 && x <= 24 && (y == 16 || y == 27 || x == 7 || x == 24 || (y % 3 == 1 && x > 9 && x < 22));
            data.set(x, y, edge || shutter || label);
        }
}

} // namespace

Rsc makeExample()
{
    Rsc r;

    // ---------------------------------------------------------------- menu
    Tree menu = makeMenuTree("MAINMENU");
    {
        MenuInfo mi;
        analyseMenu(menu, mi);
        menu.obj[mi.titles[0]].name = "MT_DESK";
        menu.obj[mi.titles[1]].name = "MT_FILE";
        int about = childrenOf(menu, mi.boxes[0])[0];
        menu.obj[about].str = "  About GEMWay demo...";
        menu.obj[about].name = "MI_ABOUT";
        int quit = childrenOf(menu, mi.boxes[1])[0];
        Object& q = menu.obj[quit];
        q.str = "  Open...    ^O"; q.name = "MI_OPEN";
        int s = menuAddItem(menu, mi.boxes[1], "  Save       ^S", false);
        menu.obj[s].name = "MI_SAVE";
        menuAddItem(menu, mi.boxes[1], "", true);
        int qu = menuAddItem(menu, mi.boxes[1], "  Quit       ^Q", false);
        menu.obj[qu].name = "MI_QUIT";
        int t = menuAddTitle(menu, " Options ");
        menu.obj[t].name = "MT_OPTIONS";
        analyseMenu(menu, mi);
        int g = menuAddItem(menu, mi.boxes[2], "  Show grid", false);
        menu.obj[g].state |= CHECKED;
        menu.obj[g].name = "MI_GRID";
        int d = menuAddItem(menu, mi.boxes[2], "  Disabled item", false);
        menu.obj[d].state |= DISABLED;
        menuRelayout(menu, 8, 16);
        sortTree(menu);
    }
    r.trees.push_back(menu);

    // ---------------------------------------------------------------- dialog with every type
    Tree d = makeDialogTree("ALLTYPES", 8, 16);
    d.obj[0].w = makeRaw(60, 0);
    d.obj[0].h = makeRaw(20, 0);
    addObject(d, 0, str(2, 1, "All GEM object types"));
    {
        Object t = obj(G_TEXT, 2, 2, 20, 1);
        t.ted.text = "G_TEXT small font";
        t.ted.font = FONT_SMALL;
        addObject(d, 0, t);
    }
    {
        Object b = obj(G_BOXTEXT, 24, 1, 16, 2);
        b.ted.text = "G_BOXTEXT";
        b.ted.color = 0x1171;   // border black, text black, pattern 7? keep readable
        b.ted.color = 0x1141;
        b.ted.thickness = 2;
        b.state = SHADOWED;
        addObject(d, 0, b);
    }
    {
        Object f = obj(G_FTEXT, 2, 4, 24, 1);
        f.ted.text = "Atari";
        f.ted.tmplt = "Name: ____________";
        f.ted.valid = "XXXXXXXXXXXX";
        f.flags = EDITABLE;
        f.name = "ED_NAME";
        addObject(d, 0, f);
    }
    {
        Object f = obj(G_FBOXTEXT, 2, 6, 24, 1);
        f.ted.text = "1040";
        f.ted.tmplt = "RAM: ____ KB";
        f.ted.valid = "9999";
        f.ted.just = TE_CNTR;
        f.flags = EDITABLE;
        f.name = "ED_RAM";
        addObject(d, 0, f);
    }
    {
        Object ib = obj(G_IBOX, 2, 8, 24, 3);
        ib.spec = 0x00011100;
        ib.name = "RADIOS";
        int p = addObject(d, 0, ib);
        Object l = str(1, 0, "Machine:");
        addObject(d, p, l);
        const char* m[] = { "ST", "TT", "Falcon" };
        for (int i = 0; i < 3; i++) {
            Object rb = button(1 + i * 7, 1, 6, m[i], SELECTABLE | RBUTTON, std::string("RB_") + (i == 2 ? "FALCON" : m[i]));
            rb.y = makeRaw(1, 4);
            if (i == 0) rb.state = SELECTED;
            addObject(d, p, rb);
        }
    }
    {
        Object c1 = button(2, 12, 11, "Checked", SELECTABLE, "CB_CHECK");
        c1.state = CHECKED;
        addObject(d, 0, c1);
        Object c2 = button(15, 12, 11, "Crossed", SELECTABLE, "CB_CROSS");
        c2.state = CROSSED | SELECTED;
        addObject(d, 0, c2);
        Object c3 = button(2, 14, 11, "Disabled", SELECTABLE, "CB_DISABLED");
        c3.state = DISABLED;
        addObject(d, 0, c3);
        Object c4 = button(15, 14, 11, "Touch", TOUCHEXIT, "BT_TOUCH");
        addObject(d, 0, c4);
    }
    {
        Object bc = obj(G_BOXCHAR, 28, 4, 2, 1);
        bc.spec = 0x01FF1100;   // up arrow
        bc.flags = TOUCHEXIT;
        addObject(d, 0, bc);
        bc.spec = 0x02FF1100;   // down arrow
        bc.y = makeRaw(5, 0);
        addObject(d, 0, bc);
        Object box = obj(G_BOX, 31, 4, 8, 3);
        box.spec = 0x00FF1153;  // pattern 5, colour green
        box.state = OUTLINED;
        addObject(d, 0, box);
        Object ibox = obj(G_IBOX, 41, 4, 6, 3);
        ibox.spec = 0x00021100;
        addObject(d, 0, ibox);
    }
    {
        Object im = obj(G_IMAGE, 28, 8, 4, 2);
        im.bit.img.resize(32, 32);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
                im.bit.img.set(x, y, ((x / 4) + (y / 4)) % 2 == 0 && (x - 16) * (x - 16) + (y - 16) * (y - 16) < 230);
        im.bit.color = 2;
        im.w = makeRaw(4, 0);
        im.h = makeRaw(2, 0);
        im.name = "IM_BALL";
        addObject(d, 0, im);
    }
    {
        Object ic = obj(G_ICON, 33, 8, 9, 3);
        drawDisk(ic.icon.data, ic.icon.mask);
        ic.icon.text = "FLOPPY";
        ic.icon.ch = 0x1041;   // 'A'
        ic.icon.xchar = 5; ic.icon.ychar = 20;
        ic.icon.xicon = 20; ic.icon.yicon = 0; ic.icon.wicon = 32; ic.icon.hicon = 32;
        ic.icon.xtext = 0; ic.icon.ytext = 32; ic.icon.wtext = 72; ic.icon.htext = 8;
        ic.w = makeRaw(9, 0); ic.h = makeRaw(2, 8);
        ic.flags = SELECTABLE;
        ic.name = "IC_FLOPPY";
        addObject(d, 0, ic);

        Object cc = ic;
        cc.type = G_CICON;
        cc.icon.text = "COLOUR";
        cc.icon.ch = 0x1043;
        cc.x = makeRaw(44, 0);
        cc.name = "IC_COLOUR";
        CIcon ci;
        ci.planes = 4;
        int wpr = 2, h = 32;
        ci.col.assign(size_t(wpr) * h * 4, 0);
        ci.colMask = cc.icon.mask.words;
        static const int pens[] = { 4, 2, 1, 6 };   // blue, green, red, cyan (hardware values)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                if (!cc.icon.mask.get(x, y)) continue;
                int v = cc.icon.data.get(x, y) ? 15 : pens[(y / 8) & 3];
                ci.setPixel(ci.col, wpr, h, x, y, v);
            }
        ci.hasSel = true;
        ci.sel.assign(ci.col.size(), 0);
        ci.selMask = ci.colMask;
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
                if (cc.icon.mask.get(x, y)) ci.setPixel(ci.sel, wpr, h, x, y, cc.icon.data.get(x, y) ? 0 : 9);
        cc.cicons.push_back(ci);
        addObject(d, 0, cc);
    }
    {
        Object ud = obj(G_USERDEF, 50, 1, 8, 2);
        ud.name = "UD_GRAPH";
        addObject(d, 0, ud);
    }
    addObject(d, 0, button(36, 17, 10, "OK", SELECTABLE | DEFAULT | EXIT, "BT_OK"));
    addObject(d, 0, button(48, 17, 10, "Cancel", SELECTABLE | EXIT, "BT_CANCEL"));
    r.trees.push_back(d);

    // ---------------------------------------------------------------- 3D dialog (AES 3.40+)
    Tree t3 = makeDialogTree("DIALOG3D", 8, 16);
    t3.obj[0].flags = FL3DBAK;
    t3.obj[0].w = makeRaw(40, 0);
    t3.obj[0].h = makeRaw(10, 0);
    addObject(t3, 0, str(2, 1, "3D objects (TOS 4 / MultiTOS)"));
    {
        Object f = obj(G_FTEXT, 2, 3, 30, 1);
        f.ted.text = "";
        f.ted.tmplt = "File: ________.___";
        f.ted.valid = "FFFFFFFFFFF";
        f.flags = EDITABLE | FL3DBAK;
        f.name = "ED_FILE";
        addObject(t3, 0, f);
        Object a = button(2, 5, 10, "Indicator", SELECTABLE | FL3DIND, "B3_IND");
        addObject(t3, 0, a);
        Object b = button(14, 5, 10, "Selected", SELECTABLE | FL3DIND, "B3_SEL");
        b.state = SELECTED;
        addObject(t3, 0, b);
        Object c = obj(G_BOXCHAR, 26, 5, 2, 1);
        c.spec = 0x03FF1100;
        c.flags = TOUCHEXIT | FL3DACT;
        addObject(t3, 0, c);
        addObject(t3, 0, button(16, 8, 10, "OK", SELECTABLE | DEFAULT | EXIT | FL3DACT, "B3_OK"));
        addObject(t3, 0, button(28, 8, 10, "Cancel", SELECTABLE | EXIT | FL3DACT, "B3_CANCEL"));
    }
    r.trees.push_back(t3);

    // ---------------------------------------------------------------- free strings & alerts
    r.strings.push_back({ "AL_QUIT", "[2][Do you really want|to quit the demo?][Quit|Cancel]" });
    r.strings.push_back({ "AL_ERROR", "[3][Disk error!|The file could not be|written.][Retry|Cancel]" });
    r.strings.push_back({ "AL_INFO", "[1][GEMWay demo resource|for all Atari AES|versions.][ OK ]" });
    r.strings.push_back({ "ST_TITLE", " GEMWay demo " });

    // ---------------------------------------------------------------- free image
    FreeImage fi;
    fi.name = "IMG_LOGO";
    fi.bit.img.resize(48, 24);
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 48; x++) {
            // three stripes like a stylised "fuji"
            int cx = x - 24;
            bool stripe = (std::abs(cx) <= 2) || (std::abs(cx - (y - 24) / 2 + 9) <= 2 && x < 22) ||
                          (std::abs(cx + (y - 24) / 2 - 9) <= 2 && x > 26);
            fi.bit.img.set(x, y, stripe && y > 2);
        }
    r.images.push_back(fi);
    return r;
}

} // namespace rsc
