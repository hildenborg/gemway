// GEMWay - a GEM resource designer for Wayland.
#include "base/bytes.h"
#include "base/png.h"
#include "gem/scene.h"
#include "rsc/rsc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int runGui(const std::string& file, int aes, int res);   // app/app.cpp
int snapshotGui(const std::string& file, const std::string& out, const std::vector<std::string>& steps);

namespace {

void usage()
{
    printf("GEMWay - GEM resource designer for Atari computers\n\n"
           "usage:\n"
           "  gemway [file.rsc]                       start the designer\n"
           "  gemway --dump file.rsc                  print the resource structure\n"
           "  gemway --render file.rsc ITEM out.png   render an item to PNG\n"
           "         ITEM: tree index/name, s<n> free string/alert, i<n> free image\n"
           "         options: --aes N --res N --zoom N --menu N\n"
           "  gemway --alert \"[1][Text][OK]\" out.png  render an alert string\n"
           "  gemway --convert in.rsc out.rsc         load and re-save (also writes .HRD)\n"
           "  gemway --header file.rsc [c|pascal|gfa|asm|modula|rsh]\n"
           "  gemway --make-example out.rsc           write a demo resource (+ .HRD, .H)\n"
           "  gemway --ui-snapshot out.png [file.rsc] [steps]   render the designer UI headless\n"
           "  gemway --modes                          list AES versions and resolutions\n");
}

void listModes()
{
    printf("AES versions (--aes):\n");
    for (size_t i = 0; i < gem::aesProfiles().size(); i++) printf("  %zu  %s\n", i, gem::aesProfiles()[i].name);
    printf("Resolutions (--res):\n");
    for (size_t i = 0; i < gem::resolutions().size(); i++) printf("  %zu  %s\n", i, gem::resolutions()[i].name);
}

bool savePng(const gem::Screen& s, const std::string& path, int zoom, int ax, int ay)
{
    int zx = zoom * ax, zy = zoom * ay;
    int w = s.w * zx, h = s.h * zy;
    std::vector<uint32_t> px(size_t(w) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) px[size_t(y) * w + x] = s.rgbAt(x / zx, y / zy);
    return base::writePng(path, w, h, px);
}

int dump(const std::string& path)
{
    rsc::Rsc r;
    rsc::LoadResult lr = rsc::loadRscFile(path, r);
    if (!lr.ok) { fprintf(stderr, "error: %s\n", lr.error.c_str()); return 1; }
    for (auto& w : lr.warnings) printf("warning: %s\n", w.c_str());
    printf("%zu trees, %zu free strings, %zu free images%s\n", r.trees.size(), r.strings.size(), r.images.size(),
           r.hasColourIcons() ? ", colour icons" : "");
    for (size_t t = 0; t < r.trees.size(); t++) {
        const rsc::Tree& tr = r.trees[t];
        printf("tree %zu %s (%s, %zu objects)\n", t, tr.name.c_str(), tr.kind == rsc::TreeKind::Menu ? "menu" : "dialog",
               tr.obj.size());
        for (size_t i = 0; i < tr.obj.size(); i++) {
            const rsc::Object& o = tr.obj[i];
            std::string txt;
            if (rsc::typeHasTedinfo(o.baseType())) txt = o.ted.text + " | " + o.ted.tmplt;
            else if (rsc::typeHasString(o.baseType())) txt = o.str;
            else if (o.baseType() == rsc::G_ICON || o.baseType() == rsc::G_CICON) txt = o.icon.text;
            printf("  %3zu%*s %-10s next=%d head=%d tail=%d flags=%04X state=%04X xywh=%04X,%04X,%04X,%04X %s %s\n", i,
                   rsc::depthOf(tr, int(i)) * 2, "", rsc::shortTypeName(o.baseType()), o.next, o.head, o.tail, o.flags,
                   o.state, uint16_t(o.x), uint16_t(o.y), uint16_t(o.w), uint16_t(o.h), o.name.c_str(),
                   base::atariToUtf8(txt).c_str());
        }
    }
    for (size_t i = 0; i < r.strings.size(); i++)
        printf("string %zu %s: %s\n", i, r.strings[i].name.c_str(), base::atariToUtf8(r.strings[i].text).c_str());
    for (size_t i = 0; i < r.images.size(); i++)
        printf("image %zu %s: %dx%d\n", i, r.images[i].name.c_str(), r.images[i].bit.img.w, r.images[i].bit.img.h);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    int aes = -1, res = -1, zoom = 1, menu = -1;
    std::vector<std::string> pos;
    std::string cmd;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        auto num = [&](int& v) { if (i + 1 < args.size()) v = atoi(args[++i].c_str()); };
        if (a == "--aes") num(aes);
        else if (a == "--res") num(res);
        else if (a == "--zoom") num(zoom);
        else if (a == "--menu") num(menu);
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a.size() > 2 && a[0] == '-' && a[1] == '-' && cmd.empty()) cmd = a;
        else pos.push_back(a);
    }
    if (cmd.empty()) return runGui(pos.empty() ? "" : pos[0], aes, res);
    if (cmd == "--ui-snapshot" && !pos.empty()) {
        // --ui-snapshot out.png [file.rsc] [steps...]
        std::string file = pos.size() > 1 && pos[1].size() > 4 && base::toLower(base::pathExt(pos[1])) == ".rsc" ? pos[1] : "";
        std::vector<std::string> steps(pos.begin() + (file.empty() ? 1 : 2), pos.end());
        return snapshotGui(file, pos[0], steps);
    }
    if (cmd == "--modes") { listModes(); return 0; }
    if (cmd == "--make-example" && pos.size() == 1) {
        rsc::Rsc r = rsc::makeExample();
        std::vector<uint8_t> out;
        std::string err;
        if (!rsc::saveRsc(r, out, err) || !base::writeFile(pos[0], out)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        base::writeFile(base::pathStem(pos[0]) + ".HRD", rsc::saveNamesHrd(r));
        base::writeText(base::pathStem(pos[0]) + ".H", rsc::exportHeader(r, pos[0], rsc::HeaderLang::C));
        return 0;
    }
    if (aes < 0) aes = 1;
    if (res < 0) res = 2;
    if (cmd == "--dump" && pos.size() == 1) return dump(pos[0]);

    gem::Env env = gem::Env::make(aes, res);
    gem::Screen screen;
    screen.init(env.res.w, env.res.h, env.res.planes, env.palette, env.npal);
    gem::Aes a(screen, env);

    if (cmd == "--alert" && pos.size() == 2) {
        rsc::Rsc r;
        r.strings.push_back({ "", base::utf8ToAtari(pos[0]) });
        gem::renderItem(a, r, gem::ItemKind::String, 0, {});
        return savePng(screen, pos[1], zoom, env.res.aspectX, env.res.aspectY) ? 0 : 1;
    }
    if (cmd == "--render" && pos.size() == 3) {
        rsc::Rsc r;
        rsc::LoadResult lr = rsc::loadRscFile(pos[0], r);
        if (!lr.ok) { fprintf(stderr, "error: %s\n", lr.error.c_str()); return 1; }
        std::string item = pos[1];
        gem::ItemKind kind = gem::ItemKind::Tree;
        int idx = -1;
        if ((item[0] == 's' || item[0] == 'i') && item.size() > 1 && isdigit((unsigned char)item[1])) {
            kind = item[0] == 's' ? gem::ItemKind::String : gem::ItemKind::Image;
            idx = atoi(item.c_str() + 1);
        } else if (isdigit((unsigned char)item[0])) idx = atoi(item.c_str());
        else
            for (size_t t = 0; t < r.trees.size(); t++)
                if (base::toUpper(r.trees[t].name) == base::toUpper(item)) idx = int(t);
        if (idx < 0) { fprintf(stderr, "error: no such item '%s'\n", item.c_str()); return 1; }
        gem::SceneOptions opt;
        opt.openMenu = menu;
        gem::renderItem(a, r, kind, idx, opt);
        return savePng(screen, pos[2], zoom, env.res.aspectX, env.res.aspectY) ? 0 : 1;
    }
    if (cmd == "--convert" && pos.size() == 2) {
        rsc::Rsc r;
        rsc::LoadResult lr = rsc::loadRscFile(pos[0], r);
        if (!lr.ok) { fprintf(stderr, "error: %s\n", lr.error.c_str()); return 1; }
        std::vector<uint8_t> out;
        std::string err;
        if (!rsc::saveRsc(r, out, err)) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (!base::writeFile(pos[1], out)) { fprintf(stderr, "error: cannot write %s\n", pos[1].c_str()); return 1; }
        base::writeFile(base::pathStem(pos[1]) + ".HRD", rsc::saveNamesHrd(r));
        return 0;
    }
    if (cmd == "--header" && !pos.empty()) {
        rsc::Rsc r;
        rsc::LoadResult lr = rsc::loadRscFile(pos[0], r);
        if (!lr.ok) { fprintf(stderr, "error: %s\n", lr.error.c_str()); return 1; }
        std::string lang = pos.size() > 1 ? pos[1] : "c";
        if (lang == "rsh") { fputs(rsc::exportRsh(r, pos[0]).c_str(), stdout); return 0; }
        rsc::HeaderLang hl = lang == "pascal" ? rsc::HeaderLang::Pascal : lang == "gfa" ? rsc::HeaderLang::Gfa
                           : lang == "asm" ? rsc::HeaderLang::Asm : lang == "modula" ? rsc::HeaderLang::Modula
                           : rsc::HeaderLang::C;
        fputs(rsc::exportHeader(r, pos[0], hl).c_str(), stdout);
        return 0;
    }
    usage();
    return 1;
}
