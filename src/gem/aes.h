// Re-implementation of the AES object library drawing code (objc_draw,
// objc_find, form_alert layout, menu bar) on top of the VDI emulation.
// The drawing logic follows the Atari/DRI AES as reproduced by EmuTOS,
// including the AES 3.40+ 3D object extensions.
#pragma once
#include "gem/env.h"
#include "gem/vdi.h"
#include "rsc/rsc.h"
#include <string>
#include <vector>

namespace gem {

constexpr int MAX_DEPTH = 64;

struct Layout {
    std::vector<Rect> rel;   // pixel rect relative to the parent
    std::vector<Rect> abs;   // absolute screen rect
};

struct AlertInfo {
    int icon = 0;
    std::vector<std::string> lines, buttons;
    bool tooManyLines = false, tooManyButtons = false;
    int maxLine = 0, maxButton = 0;
};

class Aes {
public:
    Aes(Screen& s, const Env& e) : s_(s), env_(e) {}

    const Env& env() const { return env_; }
    Screen& screen() { return s_; }

    // Convert the packed RSC coordinates to pixels (rsrc_obfix) and place
    // the root at (ox, oy).
    void layout(const rsc::Tree& t, int ox, int oy, Layout& L) const;
    void drawTree(const rsc::Tree& t, const Layout& L, int start = 0, int depth = MAX_DEPTH);
    int find(const rsc::Tree& t, const Layout& L, int start, int depth, int mx, int my) const;
    // Full drawn extent of an object incl. outline, shadow, 3D growth and outer border.
    Rect extent(const rsc::Tree& t, const Layout& L, int idx) const;

    // form_center(): returns root position
    void center(int w, int h, int& x, int& y) const;

    void drawDesktop();                                   // grey/green desktop with pattern
    void drawMenuBar(const rsc::Tree& menu, const Layout& L);
    void drawDropdown(const rsc::Tree& menu, const Layout& L, int box);

    static AlertInfo parseAlert(const std::string& s);
    // Build the alert dialog exactly like form_alert() and lay it out centred.
    rsc::Tree buildAlert(const std::string& s, int defButton, Layout& L) const;

    // ob_format(): template + text -> displayed string
    static std::string formatText(int just, const std::string& raw, const std::string& tmpl);

private:
    Screen& s_;
    Env env_;

    void justDraw(const rsc::Tree& t, int idx, const Rect& r);
    void grBox(int x, int y, int w, int h, int th, int color);
    void grRect(int icol, int ipat, const Rect& r);
    void grText(int just, int font, const std::string& text, const Rect& r, int mode, int color);
    void drawIcon(int state, const rsc::IconBlk& ib, const rsc::CIcon* ci, int ox, int oy);
    void draw3dOutline(const Rect& r);
    void add3dEffect(const Rect& r, int state, int th, int color);
    int xorColor(int c) const;
    bool xorIsOk(int type, int flags, const rsc::Object& o) const;
    const SysFont& font(int f) const { return f == rsc::FONT_SMALL ? *env_.small : *env_.ibm; }
};

} // namespace gem
