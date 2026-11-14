// Symbolic name files used by resource construction sets:
//   .HRD  ORCS / Interface / RSM / EmuTOS (mixed case, 16 chars)
//   .DEF  Atari RCS 1.x  (also .RSD), upper case, 8 chars
//   .DFN  Atari RCS 2.x
//   .H    C header (parsed heuristically, comments help)
#include "rsc/rsc.h"
#include "base/bytes.h"
#include <cctype>
#include <cstring>
#include <sstream>

namespace rsc {

namespace {
enum { D_DIALOG = 0, D_MENU = 1, D_ALERT = 2, D_FREESTR = 3, D_FREEBIT = 4, D_OBJECT = 5, D_EOF = 6, D_PREFIX = 7 };

void applyName(Rsc& r, int type, int tree, int obj, const std::string& name)
{
    switch (type) {
    case D_DIALOG: case D_MENU:
        if (tree >= 0 && tree < (int)r.trees.size()) {
            r.trees[tree].name = name;
            if (type == D_MENU) r.trees[tree].kind = TreeKind::Menu;
        }
        break;
    case D_ALERT: case D_FREESTR:
        if (tree >= 0 && tree < (int)r.strings.size()) r.strings[tree].name = name;
        break;
    case D_FREEBIT:
        if (tree >= 0 && tree < (int)r.images.size()) r.images[tree].name = name;
        break;
    case D_OBJECT:
        if (tree >= 0 && tree < (int)r.trees.size() && obj >= 0 && obj < (int)r.trees[tree].obj.size())
            r.trees[tree].obj[obj].name = name;
        break;
    case D_PREFIX:
        r.prefix = name;
        break;
    }
}

std::string fixedName(const uint8_t* p, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n && p[i]; i++) s += char(p[i]);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
} // namespace

bool loadNamesHrd(const std::vector<uint8_t>& d, Rsc& r)
{
    if (d.size() < 8 || base::rd16(d, 0) != 1) return false;
    size_t p = 8;
    while (p + 6 <= d.size()) {
        int type = d[p];
        int tree = base::rd16(d, p + 2), obj = base::rd16(d, p + 4);
        p += 6;
        if (type == D_EOF) break;
        std::string name;
        while (p < d.size() && d[p]) name += char(d[p++]);
        p++;
        // for free strings/images/trees the index is held in the 'tree' field
        applyName(r, type, tree, obj, name);
    }
    return true;
}

bool loadNamesDef(const std::vector<uint8_t>& d, Rsc& r)
{
    if (d.size() < 16) return false;
    int n = base::rd16(d, 0);
    if (n < 0 || size_t(n) * 16 > d.size()) return false;
    for (int i = 0; i < n; i++) {
        const uint8_t* e = &d[size_t(i) * 16];
        int tree = e[4], obj = e[5], type = e[7];
        std::string name = fixedName(e + 8, 8);
        switch (type) {
        case 0: applyName(r, D_OBJECT, tree, obj, name); break;
        case 1: case 3: applyName(r, D_DIALOG, obj, 0, name); break;
        case 2: applyName(r, D_MENU, obj, 0, name); break;
        case 4: applyName(r, D_ALERT, obj, 0, name); break;
        case 5: applyName(r, D_FREESTR, obj, 0, name); break;
        case 6: applyName(r, D_FREEBIT, obj, 0, name); break;
        }
    }
    return true;
}

bool loadNamesDfn(const std::vector<uint8_t>& d, Rsc& r)
{
    if (d.size() < 16) return false;
    int n = d[0] | (d[1] << 8);
    if (n < 0 || size_t(n) * 16 > d.size()) return false;
    for (int i = 0; i < n; i++) {
        const uint8_t* e = &d[size_t(i) * 16];
        int obj = e[2], tree = e[3], type = e[4], ind = e[5];
        std::string name = fixedName(e + 6, 8);
        switch (type) {
        case 0: applyName(r, D_OBJECT, tree, obj, name); break;
        case 1: applyName(r, D_FREESTR, obj, 0, name); break;
        case 2: applyName(r, ind ? D_FREEBIT : D_MENU, obj, 0, name); break;
        case 3: applyName(r, D_DIALOG, obj, 0, name); break;
        case 4: applyName(r, D_ALERT, obj, 0, name); break;
        }
    }
    return true;
}

bool loadNamesH(const std::string& text, Rsc& r)
{
    struct Def { std::string name; long value; std::string comment; bool blankBefore; };
    std::vector<Def> defs;
    std::istringstream in(text);
    std::string line;
    bool blank = true;
    bool anyComment = false;
    while (std::getline(in, line)) {
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos) { blank = true; continue; }
        if (line.compare(p, 7, "#define") != 0) {
            // Pascal "NAME = 3;" / asm "NAME equ 3" are not handled; C only
            continue;
        }
        std::istringstream ls(line.substr(p + 7));
        Def d; d.blankBefore = blank; blank = false;
        std::string val;
        ls >> d.name >> val;
        if (d.name.empty() || val.empty()) continue;
        char* end = nullptr;
        d.value = strtol(val.c_str(), &end, 0);
        if (end == val.c_str()) continue;
        size_t c = line.find("/*");
        if (c == std::string::npos) c = line.find("//");
        if (c != std::string::npos) { d.comment = base::toLower(line.substr(c)); anyComment = true; }
        defs.push_back(d);
    }
    if (defs.empty()) return false;

    if (anyComment) {
        int curTree = -1;
        for (auto& d : defs) {
            const std::string& c = d.comment;
            size_t hash = c.find("tree #");
            if (c.find("in tree") != std::string::npos || c.find("object") != std::string::npos) {
                int t = curTree;
                if (hash != std::string::npos) t = atoi(c.c_str() + hash + 6);
                else {
                    // "... in tree NAME": look up tree by name
                    size_t q = c.find("in tree ");
                    if (q != std::string::npos) {
                        std::string tn = c.substr(q + 8);
                        size_t e = tn.find_first_of(" */");
                        tn = base::toUpper(tn.substr(0, e));
                        for (size_t i = 0; i < r.trees.size(); i++)
                            if (base::toUpper(r.trees[i].name) == tn) t = int(i);
                    }
                }
                applyName(r, D_OBJECT, t, int(d.value), d.name);
            } else if (c.find("alert") != std::string::npos) {
                applyName(r, D_ALERT, int(d.value), 0, d.name);
            } else if (c.find("image") != std::string::npos) {
                applyName(r, D_FREEBIT, int(d.value), 0, d.name);
            } else if (c.find("string") != std::string::npos) {
                applyName(r, D_FREESTR, int(d.value), 0, d.name);
            } else if (c.find("menu") != std::string::npos) {
                curTree = int(d.value);
                applyName(r, D_MENU, curTree, 0, d.name);
            } else if (c.find("tree") != std::string::npos || c.find("form") != std::string::npos ||
                       c.find("dial") != std::string::npos || c.find("panel") != std::string::npos) {
                curTree = int(d.value);
                applyName(r, D_DIALOG, curTree, 0, d.name);
            }
        }
        return true;
    }
    // No comments: assume erd/RCS layout where each blank-line separated
    // group starts with a tree define followed by its objects.
    int curTree = -1;
    for (auto& d : defs) {
        if (d.blankBefore && d.value == curTree + 1 && d.value < (long)r.trees.size()) {
            curTree = int(d.value);
            applyName(r, D_DIALOG, curTree, 0, d.name);
            if (r.trees[curTree].kind == TreeKind::Menu) r.trees[curTree].kind = TreeKind::Menu;
        } else if (curTree >= 0) {
            applyName(r, D_OBJECT, curTree, int(d.value), d.name);
        }
    }
    return true;
}

std::vector<uint8_t> saveNamesHrd(const Rsc& r)
{
    base::Writer w;
    w.u16(1); w.u16(0); w.u16(0); w.u16(0);
    auto entry = [&](int type, int tree, int obj, const std::string& name) {
        w.u8(uint8_t(type)); w.u8(0); w.u16(uint16_t(tree)); w.u16(uint16_t(obj));
        std::string n = name.substr(0, 16);
        w.str0(n);
    };
    if (!r.prefix.empty()) entry(D_PREFIX, 0, 0, r.prefix);
    for (size_t t = 0; t < r.trees.size(); t++) {
        const Tree& tr = r.trees[t];
        if (!tr.name.empty()) entry(tr.kind == TreeKind::Menu ? D_MENU : D_DIALOG, int(t), 0, tr.name);
        for (size_t i = 0; i < tr.obj.size(); i++)
            if (!tr.obj[i].name.empty()) entry(D_OBJECT, int(t), int(i), tr.obj[i].name);
    }
    for (size_t i = 0; i < r.strings.size(); i++)
        if (!r.strings[i].name.empty()) entry(r.strings[i].isAlert() ? D_ALERT : D_FREESTR, int(i), 0, r.strings[i].name);
    for (size_t i = 0; i < r.images.size(); i++)
        if (!r.images[i].name.empty()) entry(D_FREEBIT, int(i), 0, r.images[i].name);
    w.u8(D_EOF); w.u8(0); w.u16(0); w.u16(0); w.u8(0);
    return w.d;
}

std::vector<uint8_t> saveNamesDef(const Rsc& r)
{
    struct E { int tree, obj, ind, type; std::string name; };
    std::vector<E> es;
    for (size_t t = 0; t < r.trees.size(); t++) {
        const Tree& tr = r.trees[t];
        if (!tr.name.empty()) es.push_back({ 0, int(t), 0, tr.kind == TreeKind::Menu ? 2 : 3, tr.name });
        for (size_t i = 0; i < tr.obj.size(); i++)
            if (!tr.obj[i].name.empty()) es.push_back({ int(t), int(i), 1, 0, tr.obj[i].name });
    }
    for (size_t i = 0; i < r.strings.size(); i++)
        if (!r.strings[i].name.empty()) es.push_back({ 0, int(i), 0, r.strings[i].isAlert() ? 4 : 5, r.strings[i].name });
    for (size_t i = 0; i < r.images.size(); i++)
        if (!r.images[i].name.empty()) es.push_back({ 0, int(i), 0, 6, r.images[i].name });
    std::vector<uint8_t> d(std::max<size_t>(es.size(), 1) * 16, 0);
    for (size_t i = 0; i < es.size(); i++) {
        uint8_t* e = &d[i * 16];
        e[4] = uint8_t(es[i].tree); e[5] = uint8_t(es[i].obj); e[6] = uint8_t(es[i].ind); e[7] = uint8_t(es[i].type);
        std::string n = base::toUpper(es[i].name).substr(0, 8);
        memcpy(e + 8, n.data(), n.size());
    }
    d[0] = uint8_t(es.size() >> 8);
    d[1] = uint8_t(es.size());
    return d;
}

} // namespace rsc
