#include "app/app.h"
#include "base/bytes.h"
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace app {

using namespace platform;
namespace col = ui::col;
namespace fs = std::filesystem;

// Draw a GEM style dialog frame centred on the window; returns the content rect.
static Rect dialogFrame(ui::Canvas& c, int w, int h, const std::string& title)
{
    w = std::min(w, c.w() - 20);
    h = std::min(h, c.h() - 20);
    Rect r((c.w() - w) / 2, (c.h() - h) / 2, w, h);
    c.fill(Rect(r.x + 5, r.y + 5, r.w, r.h), 0x303030);
    c.fill(r.inset(-3), 0xFFFFFF);
    c.frame(r.inset(-4), col::line);
    c.fill(r, col::panel);
    c.frame(r, col::line);
    Rect t(r.x, r.y, r.w, 22);
    c.fill(t, col::selBg);
    c.textCentered(t, title, col::selText);
    return Rect(r.x + 12, r.y + 30, r.w - 24, r.h - 40);
}

static bool keyPressed(ui::Ui& u, uint32_t sym)
{
    for (auto it = u.in.keys.begin(); it != u.in.keys.end(); ++it)
        if (it->sym == sym) { u.in.keys.erase(it); return true; }
    return false;
}

static std::vector<std::string> wrap(const std::string& text, int cols)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string para;
    while (std::getline(in, para)) {
        std::string line;
        std::istringstream ws(para);
        std::string word;
        while (ws >> word) {
            if (!line.empty() && (int)(line.size() + 1 + word.size()) > cols) { out.push_back(line); line.clear(); }
            while ((int)word.size() > cols) { out.push_back(word.substr(0, cols)); word = word.substr(cols); }
            line += (line.empty() ? "" : " ") + word;
        }
        out.push_back(line);
    }
    return out;
}

// ================================================================= message box
struct MessageBox : Modal {
    std::string title, text;
    std::vector<std::string> buttons;
    std::function<void(int)> cb;
    bool draw(App&, ui::Ui& u) override
    {
        ui::Canvas& c = u.c;
        std::vector<std::string> lines = wrap(base::utf8ToAtari(text), 56);
        int h = 30 + (int)lines.size() * 18 + 60;
        Rect r = dialogFrame(c, 500, h, title);
        for (size_t i = 0; i < lines.size(); i++) c.text(r.x, r.y + int(i) * 18, lines[i], col::text);
        int bw = 96, gap = 10;
        int total = (int)buttons.size() * bw + ((int)buttons.size() - 1) * gap;
        int bx = r.x + r.w - total, by = r.y + r.h - 26;
        int chosen = -1;
        for (size_t i = 0; i < buttons.size(); i++) {
            Rect b(bx + int(i) * (bw + gap), by, bw, 24);
            if (i == 0) c.frame(b.inset(-2), col::line, 2);
            if (u.button(b, buttons[i])) chosen = int(i);
        }
        if (keyPressed(u, K_RETURN) || keyPressed(u, K_KP_ENTER)) chosen = 0;
        if (keyPressed(u, K_ESCAPE)) chosen = (int)buttons.size() - 1;
        if (chosen >= 0) {
            if (cb) cb(chosen);
            return false;
        }
        return true;
    }
};

void App::message(const std::string& title, const std::string& text)
{
    auto* m = new MessageBox;
    m->title = title;
    m->text = text;
    m->buttons = { "OK" };
    pushModal(m);
}

void App::ask(const std::string& title, const std::string& text, const std::vector<std::string>& buttons,
              std::function<void(int)> cb)
{
    auto* m = new MessageBox;
    m->title = title;
    m->text = text;
    m->buttons = buttons;
    m->cb = std::move(cb);
    pushModal(m);
}

// ================================================================= file dialog
struct FileDialog : Modal {
    bool save = false;
    std::string title, dir, name;          // dir: UTF-8, name: Atari charset (edited)
    std::vector<std::string> exts;
    std::function<void(const std::string&)> cb;
    struct Entry { std::string name; bool isDir; };
    std::vector<Entry> entries;
    int sel = -1, scroll = 0;
    bool showAll = false, scanned = false;
    std::string dirEdit, err;

    void scan()
    {
        entries.clear();
        err.clear();
        std::error_code ec;
        fs::path p(dir);
        if (!fs::is_directory(p, ec)) { err = "not a directory"; scanned = true; return; }
        if (p.has_parent_path() && p != p.root_path()) entries.push_back({ "..", true });
        std::vector<Entry> ds, fsv;
        for (auto it = fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::string n = it->path().filename().string();
            if (n.empty() || n[0] == '.') continue;
            std::error_code e2;
            if (it->is_directory(e2)) ds.push_back({ n, true });
            else {
                std::string e = base::toLower(base::pathExt(n));
                bool ok = showAll;
                for (auto& x : exts) if (base::toLower(x) == e) ok = true;
                if (ok) fsv.push_back({ n, false });
            }
        }
        auto byName = [](const Entry& a, const Entry& b) { return base::toLower(a.name) < base::toLower(b.name); };
        std::sort(ds.begin(), ds.end(), byName);
        std::sort(fsv.begin(), fsv.end(), byName);
        entries.insert(entries.end(), ds.begin(), ds.end());
        entries.insert(entries.end(), fsv.begin(), fsv.end());
        dirEdit = base::utf8ToAtari(dir);
        sel = -1;
        scroll = 0;
        scanned = true;
    }

    void enter(const std::string& sub)
    {
        fs::path p = sub == ".." ? fs::path(dir).parent_path() : fs::path(dir) / sub;
        dir = p.lexically_normal().string();
        scan();
    }

    bool finish(App& a)
    {
        std::string n = base::atariToUtf8(name);
        if (n.empty()) return true;
        fs::path p = fs::path(n).is_absolute() ? fs::path(n) : fs::path(dir) / n;
        std::error_code ec;
        if (fs::is_directory(p, ec)) { dir = p.lexically_normal().string(); name.clear(); scan(); return true; }
        if (!save && !fs::exists(p, ec)) { err = "file not found"; return true; }
        std::string full = p.lexically_normal().string();
        a.set.lastDir = p.parent_path().string();
        if (save && fs::exists(p, ec)) {
            auto cbCopy = cb;
            a.ask("Replace file", base::pathBase(full) + " already exists. Replace it?", { "Replace", "Cancel" },
                  [cbCopy, full](int b) { if (b == 0 && cbCopy) cbCopy(full); });
            return false;
        }
        if (cb) cb(full);
        return false;
    }

    bool draw(App& a, ui::Ui& u) override
    {
        if (!scanned) scan();
        ui::Canvas& c = u.c;
        Rect r = dialogFrame(c, 640, 520, title);
        int y = r.y;
        c.text(r.x, y + 2, "Folder", col::dim);
        if (u.textField(u.id("fd.dir"), Rect(r.x + 64, y, r.w - 64 - 70, 20), dirEdit, 1024)) {}
        if (u.button(Rect(r.x + r.w - 64, y, 64, 20), "Go") || (u.focus == u.id("fd.dir") && keyPressed(u, K_RETURN))) {
            dir = base::atariToUtf8(dirEdit);
            scan();
        }
        y += 28;
        Rect list(r.x, y, r.w - 14, r.h - 28 - 90);
        c.fill(list, col::field);
        c.frame(list, col::line);
        const int rh = 18;
        if (u.hover(list) && u.in.wheelY != 0) { scroll += int(u.in.wheelY * rh * 3); u.in.wheelY = 0; }
        u.scrollbar(u.id("fd.sb"), Rect(list.x + list.w, list.y, 14, list.h), (int)entries.size() * rh, list.h - 4, scroll);
        c.pushClip(list.inset(1));
        int activate = -1;
        for (size_t i = 0; i < entries.size(); i++) {
            Rect rr(list.x + 1, list.y + 2 + int(i) * rh - scroll, list.w - 2, rh);
            if (rr.y + rr.h < list.y || rr.y > list.y + list.h) continue;
            bool s = int(i) == sel;
            if (s) c.fill(rr, col::selBg);
            else if (u.hover(rr)) c.fill(rr, col::hover);
            std::string label = (entries[i].isDir ? "\x07 " : "  ") + base::utf8ToAtari(entries[i].name) +
                                (entries[i].isDir ? "/" : "");
            c.text(rr.x + 4, rr.y + 1, label, s ? col::selText : col::text, rr.w - 8);
            if (u.clicked(rr)) {
                sel = int(i);
                if (!entries[i].isDir) name = base::utf8ToAtari(entries[i].name);
                if (u.in.dbl) activate = int(i);
            }
        }
        c.popClip();
        if (entries.empty()) c.text(list.x + 8, list.y + 8, err.empty() ? "(no matching files)" : err, col::dim);
        y = list.y + list.h + 8;
        c.text(r.x, y + 2, "File", col::dim);
        ui::Id nameId = u.id("fd.name");
        if (save && !u.focus) u.focusField(nameId);
        u.textField(nameId, Rect(r.x + 64, y, r.w - 64, 20), name, 255);
        y += 28;
        bool sa = showAll;
        if (u.checkbox(Rect(r.x, y, 200, 20), "Show all files", sa)) { showAll = sa; scan(); }
        std::string filt = "Filter:";
        for (auto& e : exts) filt += " *" + base::toUpper(e);
        c.text(r.x + 210, y + 2, filt, col::dim);
        if (!err.empty() && !entries.empty()) c.text(r.x + 210, y + 22, err, col::warn);
        bool ok = u.button(Rect(r.x + r.w - 210, r.y + r.h - 24, 100, 24), save ? "Save" : "Open");
        bool cancel = u.button(Rect(r.x + r.w - 100, r.y + r.h - 24, 100, 24), "Cancel");
        if (keyPressed(u, K_RETURN) || keyPressed(u, K_KP_ENTER)) ok = true;
        if (keyPressed(u, K_ESCAPE)) cancel = true;
        if (keyPressed(u, K_BACKSPACE) && u.focus == 0) enter("..");
        if (activate >= 0) {
            if (entries[activate].isDir) { enter(entries[activate].name); return true; }
            name = base::utf8ToAtari(entries[activate].name);
            ok = true;
        }
        if (cancel) return false;
        if (ok) {
            if (sel >= 0 && sel < (int)entries.size() && entries[sel].isDir && name.empty()) {
                enter(entries[sel].name);
                return true;
            }
            return finish(a);
        }
        return true;
    }
};

void App::fileDialog(bool save, const std::string& title, const std::string& defName, const std::vector<std::string>& exts,
                     std::function<void(const std::string&)> cb)
{
    auto* d = new FileDialog;
    d->save = save;
    d->title = title;
    d->name = base::utf8ToAtari(defName);
    d->exts = exts;
    d->cb = std::move(cb);
    std::error_code ec;
    std::string start = !path.empty() ? base::pathDir(path) : set.lastDir;
    if (start.empty() || !fs::is_directory(start, ec)) start = fs::current_path(ec).string();
    d->dir = start;
    pushModal(d);
}

// ================================================================= settings
struct SettingsDialog : Modal {
    bool draw(App& a, ui::Ui& u) override
    {
        ui::Canvas& c = u.c;
        Rect r = dialogFrame(c, 520, 380, "Save options");
        int y = r.y;
        c.text(r.x, y, "When saving a resource also write:", col::text);
        y += 24;
        u.checkbox(Rect(r.x + 10, y, 400, 20), "Name file .HRD (ORCS / Interface / RSM)", a.set.writeHrd);
        y += 24;
        u.checkbox(Rect(r.x + 10, y, 400, 20), "Name file .DEF (Atari RCS 1.x, 8 char names)", a.set.writeDef);
        y += 24;
        u.checkbox(Rect(r.x + 10, y, 220, 20), "Header file in", a.set.writeH);
        static const std::vector<std::string> langs = { "C (.H)", "Pascal (.I)", "GFA BASIC (.LST)",
                                                        "Assembler (.S)", "Modula-2 (.M2)" };
        u.dropdown(u.id("set.lang"), Rect(r.x + 160, y, 200, 20), a.set.headerLang, langs);
        y += 24;
        u.checkbox(Rect(r.x + 10, y, 400, 20), "Resource as C source .RSH", a.set.writeRsh);
        y += 36;
        c.text(r.x, y, "Alert icons", col::text);
        y += 22;
        c.text(r.x + 10, y, "Built-in icons come from EmuTOS. To use the", col::dim);
        y += 18;
        c.text(r.x + 10, y, "originals, load an RSC whose first 3 free images", col::dim);
        y += 18;
        c.text(r.x + 10, y, "are the 32x32 NOTE, QUESTION and STOP icons.", col::dim);
        y += 24;
        if (u.button(Rect(r.x + 10, y, 220, 22), "Load alert icons from RSC...")) {
            a.fileDialog(false, "Alert icon resource", "", { ".rsc" }, [&a](const std::string& f) {
                rsc::Rsc r;
                rsc::LoadResult lr = rsc::loadRscFile(f, r, false);
                if (!lr.ok || r.images.size() < 3) { a.message("Alert icons", "The file needs at least 3 free images."); return; }
                uint16_t icons[3][64] = {};
                for (int i = 0; i < 3; i++) {
                    const rsc::Bitmap& b = r.images[i].bit.img;
                    for (int y = 0; y < 32; y++)
                        for (int x = 0; x < 32; x++)
                            if (b.get(x, y)) icons[i][y * 2 + x / 16] |= uint16_t(0x8000 >> (x & 15));
                }
                gem::setAlertIcons(icons);
                a.setStatus("Alert icons loaded from " + base::pathBase(f));
            });
        }
        bool close = u.button(Rect(r.x + r.w - 100, r.y + r.h - 24, 100, 24), "Close");
        if (keyPressed(u, K_RETURN) || keyPressed(u, K_ESCAPE)) close = true;
        if (close) { a.set.save(); return false; }
        return true;
    }
};

Modal* makeSettingsDialog(App&) { return new SettingsDialog; }

// ================================================================= text pages
struct TextPage : Modal {
    std::string title;
    std::vector<std::string> lines;
    int w = 600;
    int scroll = 0;
    bool draw(App&, ui::Ui& u) override
    {
        ui::Canvas& c = u.c;
        int h = std::min(c.h() - 40, (int)lines.size() * 18 + 80);
        Rect r = dialogFrame(c, w, h, title);
        Rect body(r.x, r.y, r.w, r.h - 34);
        int content = (int)lines.size() * 18;
        if (u.hover(body) && u.in.wheelY != 0) { scroll += int(u.in.wheelY * 54); u.in.wheelY = 0; }
        scroll = std::clamp(scroll, 0, std::max(0, content - body.h));
        c.pushClip(body);
        for (size_t i = 0; i < lines.size(); i++) {
            const std::string& l = lines[i];
            bool head = !l.empty() && l[0] == '#';
            c.text(r.x, r.y + int(i) * 18 - scroll, head ? l.substr(1) : l, head ? col::accent : col::text);
        }
        c.popClip();
        bool close = u.button(Rect(r.x + r.w - 100, r.y + r.h - 24, 100, 24), "OK");
        if (keyPressed(u, K_RETURN) || keyPressed(u, K_ESCAPE)) close = true;
        return !close;
    }
};

Modal* makeAboutDialog()
{
    auto* p = new TextPage;
    p->title = "About GEMWay";
    p->w = 620;
    p->lines = {
        "#GEMWay - GEM resource designer for Atari computers",
        "",
        "Designs dialogs, menus, alerts, icons and images for the",
        "GEM AES of Atari TOS 1.x to 4.x and MultiTOS, and renders",
        "them pixel for pixel like the real machine (ST, TT, Falcon).",
        "",
        "Reads and writes standard .RSC files including TOS 4 colour",
        "icons, name files (.HRD .DEF .DFN) and writes C, Pascal,",
        "GFA BASIC, assembler and Modula-2 headers plus .RSH sources.",
        "",
        "#Credits",
        "System fonts, alert icons and TT/Falcon palettes are taken",
        "from EmuTOS (GPL-2.0-or-later, the EmuTOS development team).",
        "The drawing rules follow the AES object library as documented",
        "by Atari and reproduced by EmuTOS.",
    };
    return p;
}

Modal* makeShortcutsDialog()
{
    auto* p = new TextPage;
    p->title = "Keyboard and mouse";
    p->w = 640;
    p->lines = {
        "#Files",
        "Ctrl+N new   Ctrl+O open   Ctrl+S save   Ctrl+Shift+S save as",
        "#Editing",
        "Ctrl+Z undo   Ctrl+Y redo   Ctrl+X/C/V cut/copy/paste",
        "Ctrl+D duplicate   Del delete   Backspace select parent",
        "Ctrl+A select children   Tab / Shift+Tab next/previous object",
        "Arrows move by a character, Shift+arrows by a pixel",
        "Ctrl+arrows resize (Ctrl+Shift by pixel)",
        "Ctrl+F / Ctrl+B bring forward / send backward (draw order)",
        "Ctrl+E edit image or icon   PgUp/PgDn previous/next item",
        "#View",
        "Ctrl+wheel or Ctrl+/- zoom   wheel scroll   Shift+wheel sideways",
        "Right drag... right click: context menu   Ctrl+G grid",
        "#Mouse in the screen view",
        "Click selects, Ctrl/Shift+click adds to the selection",
        "Drag moves (snaps to the character grid; hold Alt for pixels)",
        "Drag the handles to resize; double click edits the text",
        "#Test mode (F5)",
        "Simulates form_do(): click buttons, radio buttons, type into",
        "editable fields (Tab/arrows switch fields, Esc clears),",
        "Return selects the DEFAULT button. Menus open on hover.",
    };
    return p;
}

} // namespace app
