#include "gem/scene.h"

namespace gem {

using namespace rsc;

Scene renderTree(Aes& aes, const Tree& t, const SceneOptions& opt)
{
    Scene sc;
    sc.tree = t;
    Screen& s = aes.screen();
    s.resetClip();
    if (opt.desktop) aes.drawDesktop();
    if (t.obj.empty()) return sc;
    MenuInfo mi;
    if (t.kind == TreeKind::Menu && analyseMenu(t, mi)) {
        sc.isMenu = true;
        aes.layout(sc.tree, 0, 0, sc.L);
        if (opt.openMenu >= 0 && opt.openMenu < (int)mi.titles.size()) {
            sc.tree.obj[mi.titles[opt.openMenu]].state |= SELECTED;
            if (opt.openMenu < (int)mi.boxes.size()) sc.openBox = mi.boxes[opt.openMenu];
        }
        aes.drawMenuBar(sc.tree, sc.L);
        if (sc.openBox >= 0) aes.drawDropdown(sc.tree, sc.L, sc.openBox);
        return sc;
    }
    Layout tmp;
    aes.layout(t, 0, 0, tmp);
    int x, y;
    aes.center(tmp.abs[0].w, tmp.abs[0].h, x, y);
    aes.layout(sc.tree, x, y, sc.L);
    aes.drawTree(sc.tree, sc.L, 0, MAX_DEPTH);
    return sc;
}

Scene renderItem(Aes& aes, const Rsc& r, ItemKind kind, int index, const SceneOptions& opt)
{
    if (kind == ItemKind::Tree && index >= 0 && index < (int)r.trees.size())
        return renderTree(aes, r.trees[index], opt);
    Scene sc;
    Screen& s = aes.screen();
    s.resetClip();
    if (opt.desktop) aes.drawDesktop();
    if (kind == ItemKind::String && index >= 0 && index < (int)r.strings.size()) {
        const FreeString& fs = r.strings[index];
        if (fs.isAlert()) {
            sc.tree = aes.buildAlert(fs.text, opt.alertDefault, sc.L);
            aes.drawTree(sc.tree, sc.L, 0, MAX_DEPTH);
        } else {
            // show a free string as a G_STRING inside a white box
            Tree t;
            Object b; b.type = G_BOX; b.spec = 0x00011100;
            int len = (int)fs.text.size();
            b.w = makeRaw(len + 2, 0); b.h = makeRaw(3, 0);
            addObject(t, -1, b);
            Object o; o.type = G_STRING; o.str = fs.text; o.x = makeRaw(1, 0); o.y = makeRaw(1, 0);
            o.w = makeRaw(len, 0); o.h = makeRaw(1, 0);
            addObject(t, 0, o);
            sc = renderTree(aes, t, SceneOptions{ false, -1, 1 });
        }
    } else if (kind == ItemKind::Image && index >= 0 && index < (int)r.images.size()) {
        const BitBlk& bb = r.images[index].bit;
        Tree t;
        Object b; b.type = G_BOX; b.spec = 0x00011100;
        b.w = fromPixels(bb.img.w + 16, aes.env().res.cellW);
        b.h = fromPixels(bb.img.h + 16, aes.env().res.cellH);
        addObject(t, -1, b);
        Object o; o.type = G_IMAGE; o.bit = bb; o.x = makeRaw(0, 8); o.y = makeRaw(0, 8);
        o.w = fromPixels(bb.img.w, aes.env().res.cellW); o.h = fromPixels(bb.img.h, aes.env().res.cellH);
        addObject(t, 0, o);
        sc = renderTree(aes, t, SceneOptions{ false, -1, 1 });
    }
    return sc;
}

} // namespace gem
