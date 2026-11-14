#include "app/app.h"
#include "base/bytes.h"
#include "base/png.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace app {

using namespace rsc;
namespace fs = std::filesystem;

// ================================================================= settings
static std::string configDir()
{
    const char* x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config";
    return base + "/gemway";
}

void Settings::load()
{
    std::ifstream f(configDir() + "/settings.conf");
    std::string line;
    recent.clear();
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        int n = atoi(v.c_str());
        if (k == "aes") aes = n;
        else if (k == "res") res = n;
        else if (k == "zoom") zoom = std::clamp(n, 1, 8);
        else if (k == "grid") grid = n;
        else if (k == "snap") snap = n;
        else if (k == "outlines") outlines = n;
        else if (k == "writeHrd") writeHrd = n;
        else if (k == "writeDef") writeDef = n;
        else if (k == "writeH") writeH = n;
        else if (k == "writeRsh") writeRsh = n;
        else if (k == "headerLang") headerLang = n;
        else if (k == "lastDir") lastDir = v;
        else if (k == "recent") recent.push_back(v);
    }
}

void Settings::save() const
{
    std::error_code ec;
    fs::create_directories(configDir(), ec);
    std::ofstream f(configDir() + "/settings.conf");
    f << "aes=" << aes << "\nres=" << res << "\nzoom=" << zoom << "\ngrid=" << grid << "\nsnap=" << snap
      << "\noutlines=" << outlines << "\nwriteHrd=" << writeHrd << "\nwriteDef=" << writeDef << "\nwriteH=" << writeH
      << "\nwriteRsh=" << writeRsh << "\nheaderLang=" << headerLang << "\nlastDir=" << lastDir << "\n";
    for (auto& r : recent) f << "recent=" << r << "\n";
}

// ================================================================= helpers
const char* colourName(int c)
{
    static const char* n[] = { "White", "Black", "Red", "Green", "Blue", "Cyan", "Yellow", "Magenta",
                                "Light grey", "Dark grey", "Light red", "Light green", "Light blue", "Light cyan",
                                "Light yellow", "Light magenta" };
    return c >= 0 && c < 16 ? n[c] : "?";
}

std::vector<std::string> colourNames(int n)
{
    std::vector<std::string> v;
    for (int i = 0; i < n; i++) v.push_back(std::to_string(i) + " " + colourName(i));
    return v;
}

void drawSwatch(ui::Canvas& c, const Rect& r, uint32_t rgb)
{
    c.fill(r, rgb);
    c.frame(r, ui::col::line);
}

std::string itemLabel(const Rsc& r, ItemKind k, int idx)
{
    char buf[128];
    switch (k) {
    case ItemKind::Tree: {
        const Tree& t = r.trees[idx];
        snprintf(buf, sizeof buf, "%s %s", t.kind == TreeKind::Menu ? "\xF0" : "#",
                 t.name.empty() ? ("tree " + std::to_string(idx)).c_str() : t.name.c_str());
        return buf;
    }
    case ItemKind::String: {
        const FreeString& s = r.strings[idx];
        std::string n = s.name.empty() ? (s.isAlert() ? "alert " : "string ") + std::to_string(idx) : s.name;
        snprintf(buf, sizeof buf, "%s %s", s.isAlert() ? "!" : "\"", n.c_str());
        return buf;
    }
    case ItemKind::Image: {
        const FreeImage& im = r.images[idx];
        snprintf(buf, sizeof buf, "@ %s", im.name.empty() ? ("image " + std::to_string(idx)).c_str() : im.name.c_str());
        return buf;
    }
    }
    return "";
}

std::string objectLabel(const Tree& t, int idx)
{
    const Object& o = t.obj[idx];
    std::string s = std::to_string(idx) + " " + shortTypeName(o.baseType());
    if (o.extType()) s += "+" + std::to_string(o.extType());
    if (!o.name.empty()) s += " " + o.name;
    std::string txt;
    if (typeHasTedinfo(o.baseType())) txt = o.ted.text.empty() ? o.ted.tmplt : o.ted.text;
    else if (typeHasString(o.baseType())) txt = o.str;
    else if (o.baseType() == G_BOXCHAR && (o.spec >> 24)) txt = std::string(1, char(o.spec >> 24));
    else if (o.baseType() == G_ICON || o.baseType() == G_CICON) txt = o.icon.text;
    if (!txt.empty()) s += " \"" + txt + "\"";
    return s;
}

// ================================================================= app core
App::App()
{
    set.load();
    setMode(set.aes, set.res);
}

void App::setMode(int aes, int res)
{
    set.aes = std::clamp(aes, 0, (int)gem::aesProfiles().size() - 1);
    set.res = std::clamp(res, 0, (int)gem::resolutions().size() - 1);
    env = gem::Env::make(set.aes, set.res);
    screen.init(env.res.w, env.res.h, env.res.planes, env.palette, env.npal);
    fitPending = true;
    redraw_ = true;
}

int App::run(const std::string& file, int aes, int res)
{
    if (aes >= 0 || res >= 0) setMode(aes >= 0 ? aes : set.aes, res >= 0 ? res : set.res);
    newDoc();
    if (!file.empty()) openDoc(file);
    if (!win.open("GEMWay", 1400, 900, this)) {
        fprintf(stderr, "gemway: %s\n", win.error().c_str());
        fprintf(stderr, "gemway: use --help for command line (headless) functions\n");
        return 1;
    }
    updateTitle();
    int rc = win.run();
    if (rc) fprintf(stderr, "gemway: %s\n", win.error().c_str());
    set.save();
    return rc;
}

void App::updateTitle()
{
    std::string t = "GEMWay - " + (path.empty() ? std::string("untitled.rsc") : base::pathBase(path));
    if (dirty) t += " *";
    win.setTitle(t);
}

// ---------------------------------------------------------------- input
void App::onPointer(double x, double y)
{
    pending.mx = int(x);
    pending.my = int(y);
    redraw_ = true;
}

void App::onButton(int b, bool down)
{
    static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    static int lx = -100, ly = -100;
    pending.down[b] = down;
    if (down) {
        pending.pressed[b] = true;
        if (b == 0) {
            auto now = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
            pending.dbl = ms < 400 && std::abs(pending.mx - lx) < 5 && std::abs(pending.my - ly) < 5;
            last = pending.dbl ? now - std::chrono::seconds(10) : now;
            lx = pending.mx;
            ly = pending.my;
        }
    } else {
        pending.released[b] = true;
    }
    redraw_ = true;
}

void App::onScroll(double dx, double dy)
{
    pending.wheelX += dx;
    pending.wheelY += dy;
    redraw_ = true;
}

void App::onKey(const platform::KeyEvent& e)
{
    pending.keys.push_back(e);
    pending.mods = e.mods;
    redraw_ = true;
}

void App::onPointerLeave()
{
    pending.mx = pending.my = -10000;
    redraw_ = true;
}

bool App::onClose()
{
    quit();
    return false;
}

void App::quit()
{
    confirmDiscard([this]() { win.quit(); });
}

void App::render(uint32_t* px, int w, int h, int stride, int scale)
{
    canvas.begin(px, w, h, stride, scale);
    bool hadInput = pending.pressed[0] || pending.pressed[1] || pending.pressed[2] || pending.released[0] ||
                    pending.released[1] || pending.released[2] || !pending.keys.empty() || pending.wheelX != 0 ||
                    pending.wheelY != 0;
    static int lmx = -1, lmy = -1;
    if (pending.mx != lmx || pending.my != lmy) hadInput = true;
    lmx = pending.mx;
    lmy = pending.my;
    u.in = pending;
    pending.keys.clear();
    for (int i = 0; i < 3; i++) pending.pressed[i] = pending.released[i] = false;
    pending.dbl = false;
    pending.wheelX = pending.wheelY = 0;
    // Requests made while building the frame (commands run from menu
    // results, closing modals, ...) must survive, so clear before frame().
    redraw_ = false;
    frame();
    // Popup choices are delivered on the following frame: keep drawing
    // until they have been consumed.
    redraw_ = redraw_ || hadInput || u.hasPendingResult();
}

// ---------------------------------------------------------------- undo
void App::checkpoint(ui::Id session)
{
    if (session != 0 && session == lastEdit_) { touch(); return; }
    lastEdit_ = session;
    undo_.push_back(doc);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    touch();
}

void App::undo()
{
    if (undo_.empty()) return;
    redo_.push_back(doc);
    doc = std::move(undo_.back());
    undo_.pop_back();
    lastEdit_ = 0;
    u.dropFocus();
    clampSelection();
    touch();
    setStatus("Undo");
}

void App::redo()
{
    if (redo_.empty()) return;
    undo_.push_back(doc);
    doc = std::move(redo_.back());
    redo_.pop_back();
    lastEdit_ = 0;
    u.dropFocus();
    clampSelection();
    touch();
    setStatus("Redo");
}

// ---------------------------------------------------------------- selection
Tree* App::curTree()
{
    if (selKind != ItemKind::Tree || selItem < 0 || selItem >= (int)doc.trees.size()) return nullptr;
    return &doc.trees[selItem];
}

void App::selectItem(ItemKind k, int idx)
{
    if (k != selKind || idx != selItem) {
        selObjs.clear();
        openMenuTitle = -1;
        inspectorScroll = 0;
        scrollX = scrollY = 0;
        u.dropFocus();
    }
    selKind = k;
    selItem = idx;
    if (testMode) enterTestMode(true);
    clampSelection();
}

void App::selectObj(int obj, bool toggle)
{
    Tree* t = curTree();
    if (!t || obj < 0 || obj >= (int)t->obj.size()) { selObjs.clear(); return; }
    auto it = std::find(selObjs.begin(), selObjs.end(), obj);
    if (toggle) {
        if (it != selObjs.end()) selObjs.erase(it);
        else selObjs.push_back(obj);
    } else {
        selObjs.assign(1, obj);
    }
    // keep the menu dropdown of the selected object open
    MenuInfo mi;
    if (t->kind == TreeKind::Menu && analyseMenu(*t, mi)) {
        for (size_t i = 0; i < mi.titles.size(); i++)
            if (obj == mi.titles[i] || (i < mi.boxes.size() && (obj == mi.boxes[i] || isAncestor(*t, mi.boxes[i], obj))))
                openMenuTitle = int(i);
    }
}

bool App::isSelected(int obj) const { return std::find(selObjs.begin(), selObjs.end(), obj) != selObjs.end(); }

void App::clampSelection()
{
    int n = 0;
    switch (selKind) {
    case ItemKind::Tree: n = (int)doc.trees.size(); break;
    case ItemKind::String: n = (int)doc.strings.size(); break;
    case ItemKind::Image: n = (int)doc.images.size(); break;
    }
    if (selItem >= n) selItem = n - 1;
    if (selItem < 0 && n > 0) selItem = 0;
    if (selItem < 0) {
        if (!doc.trees.empty()) { selKind = ItemKind::Tree; selItem = 0; }
        else if (!doc.strings.empty()) { selKind = ItemKind::String; selItem = 0; }
        else if (!doc.images.empty()) { selKind = ItemKind::Image; selItem = 0; }
    }
    Tree* t = curTree();
    std::vector<int> keep;
    if (t)
        for (int o : selObjs) if (o >= 0 && o < (int)t->obj.size()) keep.push_back(o);
    selObjs = keep;
}

// ---------------------------------------------------------------- files
void App::newDoc()
{
    doc = Rsc();
    doc.trees.push_back(makeDialogTree("DIALOG1", cellW(), cellH()));
    Tree& t = doc.trees[0];
    Object title = makeObject(G_STRING, cellW(), cellH());
    title.str = "New dialog";
    title.x = makeRaw(2, 0); title.y = makeRaw(1, 0); title.w = makeRaw(10, 0);
    rsc::addObject(t, 0, title);
    Object ok = makeObject(G_BUTTON, cellW(), cellH());
    ok.str = "OK";
    ok.name = "OK";
    ok.flags = SELECTABLE | DEFAULT | EXIT;
    ok.x = makeRaw(30, 0); ok.y = makeRaw(10, 0); ok.w = makeRaw(8, 0);
    rsc::addObject(t, 0, ok);
    path.clear();
    dirty = false;
    undo_.clear();
    redo_.clear();
    selKind = ItemKind::Tree;
    selItem = 0;
    selObjs.clear();
    openMenuTitle = -1;
    testMode = false;
    updateTitle();
}

bool App::openDoc(const std::string& file)
{
    Rsc r;
    LoadResult lr = loadRscFile(file, r);
    if (!lr.ok) {
        message("Open failed", base::pathBase(file) + ": " + lr.error);
        return false;
    }
    doc = std::move(r);
    path = file;
    dirty = false;
    undo_.clear();
    redo_.clear();
    selKind = ItemKind::Tree;
    selItem = -1;
    selObjs.clear();
    openMenuTitle = -1;
    testMode = false;
    clampSelection();
    set.lastDir = base::pathDir(file);
    addRecent(file);
    updateTitle();
    std::string st = "Loaded " + base::pathBase(file) + ": " + std::to_string(doc.trees.size()) + " trees, " +
                     std::to_string(doc.strings.size()) + " strings, " + std::to_string(doc.images.size()) + " images";
    if (!lr.warnings.empty()) st += " (" + std::to_string(lr.warnings.size()) + " warnings: " + lr.warnings[0] + ")";
    setStatus(st);
    return true;
}

bool App::saveDoc(const std::string& file)
{
    std::vector<uint8_t> data;
    std::string err;
    if (!saveRsc(doc, data, err)) { message("Save failed", err); return false; }
    if (!base::writeFile(file, data)) { message("Save failed", "Cannot write " + file); return false; }
    std::string stem = base::pathStem(file);
    bool upper = base::pathExt(file) == base::toUpper(base::pathExt(file));
    auto ext = [&](const char* e) { return stem + "." + (upper ? base::toUpper(e) : base::toLower(e)); };
    std::vector<std::string> extra;
    if (set.writeHrd) { base::writeFile(ext("hrd"), saveNamesHrd(doc)); extra.push_back("HRD"); }
    if (set.writeDef) { base::writeFile(ext("def"), saveNamesDef(doc)); extra.push_back("DEF"); }
    if (set.writeH) {
        static const char* exts[] = { "h", "i", "lst", "s", "m2" };
        int l = std::clamp(set.headerLang, 0, 4);
        base::writeText(ext(exts[l]), exportHeader(doc, file, HeaderLang(l)));
        extra.push_back(base::toUpper(exts[l]));
    }
    if (set.writeRsh) { base::writeText(ext("rsh"), exportRsh(doc, file)); extra.push_back("RSH"); }
    path = file;
    dirty = false;
    set.lastDir = base::pathDir(file);
    addRecent(file);
    updateTitle();
    std::string st = "Saved " + base::pathBase(file) + " (" + std::to_string(data.size()) + " bytes";
    for (auto& e : extra) st += ", " + e;
    st += ")";
    std::vector<std::string> probs = checkNames(doc);
    if (!probs.empty()) st += "  Warning: " + probs[0];
    setStatus(st);
    return true;
}

void App::addRecent(const std::string& f)
{
    std::string a = fs::absolute(f).lexically_normal().string();
    set.recent.erase(std::remove(set.recent.begin(), set.recent.end(), a), set.recent.end());
    set.recent.insert(set.recent.begin(), a);
    if (set.recent.size() > 8) set.recent.resize(8);
    set.save();
}

void App::confirmDiscard(std::function<void()> then)
{
    if (!dirty) { then(); return; }
    ask("Unsaved changes", "Save the changes to " + (path.empty() ? std::string("untitled.rsc") : base::pathBase(path)) + "?",
        { "Save", "Discard", "Cancel" }, [this, then](int b) {
            if (b == 0) {
                if (path.empty()) {
                    fileDialog(true, "Save resource as", "UNTITLED.RSC", { ".rsc" }, [this, then](const std::string& f) {
                        if (saveDoc(f)) then();
                    });
                } else if (saveDoc(path)) then();
            } else if (b == 1) then();
        });
}

void App::cmdOpen()
{
    confirmDiscard([this]() {
        fileDialog(false, "Open GEM resource", "", { ".rsc" }, [this](const std::string& f) { openDoc(f); });
    });
}

void App::cmdSave()
{
    if (path.empty()) cmdSaveAs();
    else saveDoc(path);
}

void App::cmdSaveAs()
{
    std::string def = path.empty() ? "UNTITLED.RSC" : base::pathBase(path);
    fileDialog(true, "Save resource as", def, { ".rsc" }, [this](const std::string& f) {
        std::string g = f;
        if (base::pathExt(g).empty()) g += ".RSC";
        saveDoc(g);
    });
}

void App::cmdExport(int which)
{
    static const char* exts[] = { ".H", ".I", ".LST", ".S", ".M2", ".RSH", ".HRD", ".DEF" };
    static const char* titles[] = { "Export C header", "Export Pascal include", "Export GFA BASIC listing",
                                    "Export assembler equates", "Export Modula-2 constants", "Export RSH C source",
                                    "Export HRD name file", "Export DEF name file (RCS 1.x)" };
    std::string stem = path.empty() ? "UNTITLED" : base::pathBase(base::pathStem(path));
    fileDialog(true, titles[which], stem + exts[which], { exts[which] }, [this, which](const std::string& f) {
        bool ok;
        std::string rscName = path.empty() ? base::pathStem(f) + ".RSC" : path;
        if (which <= 4) ok = base::writeText(f, exportHeader(doc, rscName, HeaderLang(which)));
        else if (which == 5) ok = base::writeText(f, exportRsh(doc, rscName));
        else if (which == 6) ok = base::writeFile(f, saveNamesHrd(doc));
        else ok = base::writeFile(f, saveNamesDef(doc));
        setStatus(ok ? "Exported " + base::pathBase(f) : "Export failed: " + f);
    });
}

void App::cmdImportNames()
{
    fileDialog(false, "Import names (.HRD .DEF .DFN .RSD .H)", "", { ".hrd", ".def", ".dfn", ".rsd", ".h" },
               [this](const std::string& f) {
                   std::vector<uint8_t> d;
                   if (!base::readFile(f, d)) { message("Import failed", "Cannot read " + f); return; }
                   checkpoint();
                   std::string e = base::toLower(base::pathExt(f));
                   bool ok = e == ".hrd" ? loadNamesHrd(d, doc)
                           : (e == ".def" || e == ".rsd") ? loadNamesDef(d, doc)
                           : e == ".dfn" ? loadNamesDfn(d, doc)
                           : loadNamesH(std::string(d.begin(), d.end()), doc);
                   setStatus(ok ? "Imported names from " + base::pathBase(f) : "No names found in " + base::pathBase(f));
               });
}

// ================================================================= item commands
void App::addItem(int what)
{
    checkpoint();
    char nm[32];
    switch (what) {
    case 0: {
        snprintf(nm, sizeof nm, "DIALOG%zu", doc.trees.size() + 1);
        doc.trees.push_back(makeDialogTree(nm, cellW(), cellH()));
        selectItem(ItemKind::Tree, (int)doc.trees.size() - 1);
        break;
    }
    case 1: {
        snprintf(nm, sizeof nm, "MENU%zu", doc.trees.size() + 1);
        doc.trees.push_back(makeMenuTree(nm));
        selectItem(ItemKind::Tree, (int)doc.trees.size() - 1);
        break;
    }
    case 2: case 3: {
        FreeString s;
        snprintf(nm, sizeof nm, what == 2 ? "ALERT%zu" : "STRING%zu", doc.strings.size() + 1);
        s.name = nm;
        s.text = what == 2 ? "[1][This is an alert.|Edit the text in|the inspector.][ OK |Cancel]" : "Free string";
        doc.strings.push_back(s);
        selectItem(ItemKind::String, (int)doc.strings.size() - 1);
        break;
    }
    case 4: {
        FreeImage im;
        snprintf(nm, sizeof nm, "IMAGE%zu", doc.images.size() + 1);
        im.name = nm;
        im.bit.img.resize(32, 32);
        im.bit.color = 1;
        for (int i = 0; i < 32; i++) { im.bit.img.set(i, i, 1); im.bit.img.set(31 - i, i, 1); }
        doc.images.push_back(im);
        selectItem(ItemKind::Image, (int)doc.images.size() - 1);
        break;
    }
    }
}

void App::deleteItem()
{
    if (selItem < 0) return;
    checkpoint();
    switch (selKind) {
    case ItemKind::Tree: if (selItem < (int)doc.trees.size()) doc.trees.erase(doc.trees.begin() + selItem); break;
    case ItemKind::String: if (selItem < (int)doc.strings.size()) doc.strings.erase(doc.strings.begin() + selItem); break;
    case ItemKind::Image: if (selItem < (int)doc.images.size()) doc.images.erase(doc.images.begin() + selItem); break;
    }
    selObjs.clear();
    clampSelection();
}

void App::duplicateItem()
{
    if (selItem < 0) return;
    checkpoint();
    switch (selKind) {
    case ItemKind::Tree: {
        Tree t = doc.trees[selItem];
        t.name = t.name.empty() ? "" : t.name.substr(0, 13) + "_2";
        for (auto& o : t.obj) if (!o.name.empty()) o.name = o.name.substr(0, 13) + "_2";
        doc.trees.insert(doc.trees.begin() + selItem + 1, t);
        break;
    }
    case ItemKind::String: {
        FreeString s = doc.strings[selItem];
        if (!s.name.empty()) s.name = s.name.substr(0, 13) + "_2";
        doc.strings.insert(doc.strings.begin() + selItem + 1, s);
        break;
    }
    case ItemKind::Image: {
        FreeImage s = doc.images[selItem];
        if (!s.name.empty()) s.name = s.name.substr(0, 13) + "_2";
        doc.images.insert(doc.images.begin() + selItem + 1, s);
        break;
    }
    }
    selectItem(selKind, selItem + 1);
}

void App::moveItem(int dir)
{
    int n = selKind == ItemKind::Tree ? (int)doc.trees.size() : selKind == ItemKind::String ? (int)doc.strings.size()
                                                                                            : (int)doc.images.size();
    int to = selItem + dir;
    if (selItem < 0 || to < 0 || to >= n) return;
    checkpoint();
    if (selKind == ItemKind::Tree) std::swap(doc.trees[selItem], doc.trees[to]);
    else if (selKind == ItemKind::String) std::swap(doc.strings[selItem], doc.strings[to]);
    else std::swap(doc.images[selItem], doc.images[to]);
    selItem = to;
}

// ================================================================= object commands
static bool isContainerType(int t)
{
    return t == G_BOX || t == G_IBOX || t == G_BOXCHAR || t == G_BOXTEXT || t == G_FBOXTEXT || t == G_USERDEF;
}

void App::addObject(int type)
{
    Tree* t = curTree();
    if (!t || testMode) return;
    MenuInfo mi;
    if (t->kind == TreeKind::Menu && analyseMenu(*t, mi)) {
        // in menus "add" means a new menu entry
        if (type == G_TITLE) { menuCommand(0); return; }
        if (type == G_STRING) { menuCommand(1); return; }
    }
    int parent = primaryObj();
    if (parent < 0) parent = 0;
    if (!isContainerType(t->obj[parent].baseType())) parent = std::max(0, parentOf(*t, parent));
    checkpoint();
    Object o = makeObject(type, cellW(), cellH());
    // place below the last child
    int y = 1;
    for (int c : childrenOf(*t, parent)) {
        int bottom = toPixels(t->obj[c].y, cellH()) + toPixels(t->obj[c].h, cellH());
        y = std::max(y, (bottom + cellH() - 1) / cellH());
    }
    int ph = toPixels(t->obj[parent].h, cellH());
    if ((y + 1) * cellH() > ph) y = 1;
    o.x = makeRaw(1, 0);
    o.y = makeRaw(y, 0);
    if (type == G_IMAGE || type == G_ICON || type == G_CICON) o.y = makeRaw(y, 0);
    int idx = rsc::addObject(*t, parent, o);
    selectObj(idx);
    setStatus(std::string("Added ") + typeName(type) + " #" + std::to_string(idx));
}

// remove selected objects whose ancestors are selected too
static std::vector<int> topLevel(const Tree& t, const std::vector<int>& sel)
{
    std::vector<int> v;
    for (int o : sel) {
        bool covered = false;
        for (int p : sel) if (p != o && isAncestor(t, p, o)) covered = true;
        if (!covered && o > 0) v.push_back(o);
    }
    return v;
}

void App::deleteSelection()
{
    Tree* t = curTree();
    if (!t || testMode) return;
    std::vector<int> v = topLevel(*t, selObjs);
    if (v.empty()) return;
    checkpoint();
    // delete from highest index down so indices stay valid
    std::sort(v.rbegin(), v.rend());
    int par = parentOf(*t, v.back());
    for (int o : v) deleteObject(*t, o);
    selObjs.clear();
    if (par >= 0 && par < (int)t->obj.size()) selObjs.push_back(par);
    setStatus("Deleted " + std::to_string(v.size()) + " object(s)");
}

void App::copySelection()
{
    Tree* t = curTree();
    if (!t) return;
    std::vector<int> v = topLevel(*t, selObjs);
    if (v.empty()) return;
    clipboard = Tree();
    Object root; root.type = G_IBOX;
    rsc::addObject(clipboard, -1, root);
    for (int o : v) copySubtree(clipboard, 0, *t, o);
    setStatus("Copied " + std::to_string(v.size()) + " object(s)");
}

void App::cutSelection()
{
    copySelection();
    deleteSelection();
}

void App::paste()
{
    Tree* t = curTree();
    if (!t || clipboard.obj.size() < 2 || testMode) return;
    int parent = primaryObj();
    if (parent < 0) parent = 0;
    if (!isContainerType(t->obj[parent].baseType())) parent = std::max(0, parentOf(*t, parent));
    checkpoint();
    selObjs.clear();
    for (int c : childrenOf(clipboard, 0)) {
        int n = copySubtree(*t, parent, clipboard, c);
        selObjs.push_back(n);
    }
    setStatus("Pasted " + std::to_string(selObjs.size()) + " object(s)");
}

void App::duplicateSelection()
{
    Tree* t = curTree();
    if (!t || testMode) return;
    std::vector<int> v = topLevel(*t, selObjs);
    if (v.empty()) return;
    checkpoint();
    selObjs.clear();
    for (int o : v) {
        int par = parentOf(*t, o);
        int n = copySubtree(*t, par, *t, o);
        Object& no = t->obj[n];
        no.x = fromPixels(toPixels(no.x, cellW()) + cellW(), cellW());
        no.y = fromPixels(toPixels(no.y, cellH()) + cellH(), cellH());
        if (!no.name.empty()) no.name = no.name.substr(0, 13) + "_2";
        selObjs.push_back(n);
    }
}

void App::reorderSelection(int how)
{
    Tree* t = curTree();
    int o = primaryObj();
    if (!t || o <= 0 || testMode) return;
    int par = parentOf(*t, o);
    std::vector<int> kids = childrenOf(*t, par);
    int pos = int(std::find(kids.begin(), kids.end(), o) - kids.begin());
    int np = how == -2 ? 0 : how == 2 ? (int)kids.size() - 1 : pos + how;
    if (np < 0 || np >= (int)kids.size() || np == pos) return;
    checkpoint();
    reorderObject(*t, o, np);
    setStatus(how < 0 ? "Moved backward (drawn earlier)" : "Moved forward (drawn later)");
}

void App::moveIntoPrevSibling()
{
    Tree* t = curTree();
    int o = primaryObj();
    if (!t || o <= 0) return;
    int par = parentOf(*t, o);
    std::vector<int> kids = childrenOf(*t, par);
    auto it = std::find(kids.begin(), kids.end(), o);
    if (it == kids.begin()) return;
    int np = *(it - 1);
    checkpoint();
    // keep absolute position
    int x = toPixels(t->obj[o].x, cellW()) - toPixels(t->obj[np].x, cellW());
    int y = toPixels(t->obj[o].y, cellH()) - toPixels(t->obj[np].y, cellH());
    reparentObject(*t, o, np);
    t->obj[o].x = fromPixels(std::max(0, x), cellW());
    t->obj[o].y = fromPixels(std::max(0, y), cellH());
}

void App::moveOutToParent()
{
    Tree* t = curTree();
    int o = primaryObj();
    if (!t || o <= 0) return;
    int par = parentOf(*t, o);
    if (par <= 0) return;
    int gp = parentOf(*t, par);
    checkpoint();
    int x = toPixels(t->obj[o].x, cellW()) + toPixels(t->obj[par].x, cellW());
    int y = toPixels(t->obj[o].y, cellH()) + toPixels(t->obj[par].y, cellH());
    reparentObject(*t, o, gp);
    t->obj[o].x = fromPixels(x, cellW());
    t->obj[o].y = fromPixels(y, cellH());
}

void App::nudge(int dx, int dy, bool resize, bool pixel)
{
    Tree* t = curTree();
    if (!t || selObjs.empty() || testMode) return;
    ui::Id sess = ui::Ui::hash("nudge") + (resize ? 1 : 0);
    checkpoint(sess);
    for (int o : selObjs) {
        if (o == 0 && !resize) continue;
        Object& ob = t->obj[o];
        int16_t& a = resize ? ob.w : ob.x;
        int16_t& b = resize ? ob.h : ob.y;
        if (pixel) {
            a = fromPixels(std::max(resize ? 1 : -127, toPixels(a, cellW()) + dx), cellW());
            b = fromPixels(std::max(resize ? 1 : -127, toPixels(b, cellH()) + dy), cellH());
        } else {
            a = makeRaw(std::max(resize ? 1 : 0, rawChars(a) + dx), rawPixOff(a));
            b = makeRaw(std::max(resize ? 1 : 0, rawChars(b) + dy), rawPixOff(b));
        }
    }
}

void App::alignSelection(int how)
{
    Tree* t = curTree();
    if (!t || selObjs.empty()) return;
    checkpoint();
    int ref = selObjs.back();
    for (int o : selObjs) {
        if (o == 0) continue;
        Object& ob = t->obj[o];
        int par = parentOf(*t, o);
        int pw = toPixels(t->obj[par].w, cellW(), true, env.res.w), ph = toPixels(t->obj[par].h, cellH());
        int w = toPixels(ob.w, cellW(), true, env.res.w), h = toPixels(ob.h, cellH());
        const Object& r = t->obj[ref];
        switch (how) {
        case 0: ob.x = fromPixels((pw - w) / 2, cellW()); break;                       // centre h in parent
        case 1: ob.y = fromPixels((ph - h) / 2, cellH()); break;                       // centre v in parent
        case 2: ob.x = r.x; break;                                                   // align left
        case 3: ob.y = r.y; break;                                                   // align top
        case 4: ob.x = fromPixels(toPixels(r.x, cellW()) + toPixels(r.w, cellW()) - w, cellW()); break;  // right
        case 5: ob.w = r.w; ob.h = r.h; break;                                       // same size
        case 6:                                                                      // snap to char grid
            ob.x = makeRaw(rawChars(ob.x), 0); ob.y = makeRaw(rawChars(ob.y), 0);
            ob.w = makeRaw(std::max(1, rawChars(ob.w)), 0); ob.h = makeRaw(std::max(1, rawChars(ob.h)), 0);
            break;
        }
    }
}

void App::fitParent()
{
    Tree* t = curTree();
    int o = primaryObj();
    if (!t || o < 0) return;
    std::vector<int> kids = childrenOf(*t, o);
    if (kids.empty()) return;
    checkpoint();
    int w = 0, h = 0;
    for (int c : kids) {
        w = std::max(w, toPixels(t->obj[c].x, cellW()) + toPixels(t->obj[c].w, cellW()));
        h = std::max(h, toPixels(t->obj[c].y, cellH()) + toPixels(t->obj[c].h, cellH()));
    }
    // leave a margin of one character
    w = (w + cellW() + cellW() - 1) / cellW() * cellW();
    h = (h + cellH() + cellH() - 1) / cellH() * cellH();
    t->obj[o].w = fromPixels(w, cellW());
    t->obj[o].h = fromPixels(h, cellH());
}

void App::sortCurrentTree()
{
    Tree* t = curTree();
    if (!t) return;
    checkpoint();
    sortTree(*t);
    selObjs.clear();
    setStatus("Objects renumbered in tree order (indices changed: regenerate your header)");
}

void App::autoName()
{
    Tree* t = curTree();
    if (!t) return;
    checkpoint();
    std::set<std::string> used;
    for (auto& tr : doc.trees) for (auto& o : tr.obj) if (!o.name.empty()) used.insert(o.name);
    int n = 0;
    std::string tn = t->name.empty() ? "T" : t->name.substr(0, 6);
    for (size_t i = 0; i < t->obj.size(); i++) {
        Object& o = t->obj[i];
        if (!o.name.empty()) continue;
        bool interesting = (o.flags & (SELECTABLE | EXIT | EDITABLE | TOUCHEXIT)) || typeHasTedinfo(o.baseType());
        if (!interesting) continue;
        std::string base;
        if (typeHasString(o.baseType())) base = makeIdentifier(o.str);
        else if (typeHasTedinfo(o.baseType())) base = makeIdentifier(o.ted.text.empty() ? o.ted.tmplt : o.ted.text);
        if (base.empty()) base = shortTypeName(o.baseType());
        base = tn + "_" + base.substr(0, 9);
        std::string nm = base;
        for (int k = 2; used.count(nm); k++) nm = base + std::to_string(k);
        used.insert(nm);
        o.name = nm;
        n++;
    }
    setStatus("Named " + std::to_string(n) + " objects");
}

void App::menuCommand(int cmd)
{
    Tree* t = curTree();
    if (!t) return;
    MenuInfo mi;
    if (!analyseMenu(*t, mi)) { setStatus("This tree does not have the GEM menu layout"); return; }
    checkpoint();
    if (cmd == 0) {
        int idx = menuAddTitle(*t, " Title ");
        menuRelayout(*t, cellW(), cellH());
        analyseMenu(*t, mi);
        if (!mi.boxes.empty()) menuAddItem(*t, mi.boxes.back(), "  Item", false);
        menuRelayout(*t, cellW(), cellH());
        selectObj(idx);
        return;
    }
    // find the dropdown box of the selection
    int box = -1;
    int o = primaryObj();
    for (size_t i = 0; i < mi.titles.size(); i++)
        if (i < mi.boxes.size() && (o == mi.titles[i] || o == mi.boxes[i] || (o >= 0 && isAncestor(*t, mi.boxes[i], o))))
            box = mi.boxes[i];
    if (box < 0 && openMenuTitle >= 0 && openMenuTitle < (int)mi.boxes.size()) box = mi.boxes[openMenuTitle];
    if (cmd == 1 || cmd == 2) {
        if (box < 0) { setStatus("Select a menu title or item first"); return; }
        int idx = menuAddItem(*t, box, "  New item", cmd == 2);
        menuRelayout(*t, cellW(), cellH());
        selectObj(idx);
    } else if (cmd == 3) {
        menuRelayout(*t, cellW(), cellH());
        setStatus("Menu layout recalculated");
    } else if (cmd == 4) {
        // delete title together with its dropdown
        for (size_t i = 0; i < mi.titles.size(); i++)
            if (o == mi.titles[i] && i < mi.boxes.size()) {
                if (i == 0) { setStatus("The first (Desk) menu cannot be removed"); return; }
                int b = mi.boxes[i];
                deleteObject(*t, std::max(b, mi.titles[i]));
                deleteObject(*t, std::min(b, mi.titles[i]));
                selObjs.clear();
                openMenuTitle = -1;
                menuRelayout(*t, cellW(), cellH());
                return;
            }
        setStatus("Select a menu title to delete");
    }
}

void App::editBitmap()
{
    if (selKind == ItemKind::Image && selItem >= 0) { pushModal(makeBitmapEditor(*this)); return; }
    Tree* t = curTree();
    int o = primaryObj();
    if (!t || o < 0) return;
    int ty = t->obj[o].baseType();
    if (ty == G_IMAGE || ty == G_ICON || ty == G_CICON) pushModal(makeBitmapEditor(*this));
}

// ================================================================= test mode
void App::enterTestMode(bool on)
{
    testMode = on;
    testEdit = -1;
    Tree* t = curTree();
    if (on && t) {
        testTree = *t;
        // first editable field gets the cursor like form_do()
        for (size_t i = 0; i < testTree.obj.size(); i++)
            if ((testTree.obj[i].flags & EDITABLE) && !(testTree.obj[i].flags & HIDETREE)) {
                testEdit = int(i);
                testCursor = (int)testTree.obj[i].ted.text.size();
                if (!testTree.obj[i].ted.text.empty() && testTree.obj[i].ted.text[0] == '@') {
                    testTree.obj[i].ted.text.clear();
                    testCursor = 0;
                }
                break;
            }
        setStatus("Test mode: click buttons, type into fields, Esc leaves test mode");
    } else {
        setStatus("Edit mode");
    }
    u.dropFocus();
}

} // namespace app

int runGui(const std::string& file, int aes, int res)
{
    static app::App a;   // static: large object, lives for the program's lifetime
    return a.run(file, aes, res);
}

// Headless rendering of the designer UI (used for documentation and tests).
int snapshotGui(const std::string& file, const std::string& out, const std::vector<std::string>& steps)
{
    static app::App a;
    a.newDoc();
    if (!file.empty() && !a.openDoc(file)) return 1;
    int W = 1400, H = 900;
    std::vector<uint32_t> buf(size_t(W) * H);
    auto frame = [&]() { a.render(buf.data(), W, H, W, 1); };
    frame();
    for (size_t i = 0; i < steps.size(); i++) {
        const std::string& s = steps[i];
        auto arg = [&]() { return i + 1 < steps.size() ? steps[++i] : std::string(); };
        if (s == "tree") a.selectItem(gem::ItemKind::Tree, atoi(arg().c_str()));
        else if (s == "string") a.selectItem(gem::ItemKind::String, atoi(arg().c_str()));
        else if (s == "image") a.selectItem(gem::ItemKind::Image, atoi(arg().c_str()));
        else if (s == "obj") a.selectObj(atoi(arg().c_str()));
        else if (s == "menu") a.openMenuTitle = atoi(arg().c_str());
        else if (s == "aes") a.setMode(atoi(arg().c_str()), a.set.res);
        else if (s == "res") a.setMode(a.set.aes, atoi(arg().c_str()));
        else if (s == "zoom") a.set.zoom = atoi(arg().c_str());
        else if (s == "test") a.enterTestMode(true);
        else if (s == "bitmap") a.editBitmap();
        else if (s == "about") a.pushModal(app::makeAboutDialog());
        else if (s == "save") a.cmdSaveAs();
        else if (s == "click") {
            int x = atoi(arg().c_str()), y = atoi(arg().c_str());
            a.onPointer(x, y); frame();
            a.onButton(0, true); frame();
            a.onButton(0, false);
        } else if (s == "key") {
            platform::KeyEvent e; std::string k = arg();
            e.sym = (uint32_t)strtoul(k.c_str(), nullptr, 0);
            if (k.size() == 1) { e.sym = (unsigned char)k[0]; e.text = k; }
            a.onKey(e);
        } else if (s == "move") {
            int x = atoi(arg().c_str()), y = atoi(arg().c_str());
            a.onPointer(x, y);
        }
        frame();
    }
    frame();
    frame();
    std::vector<uint32_t> rgb(buf.size());
    for (size_t i = 0; i < buf.size(); i++) rgb[i] = buf[i] & 0xFFFFFF;
    return base::writePng(out, W, H, rgb) ? 0 : 1;
}
