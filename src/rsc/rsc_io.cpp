// Reading and writing of binary Atari GEM resource files.
//
// Supported: the classic RCS format (TOS 1.x .. 3.x, 64KB limit) and the
// "new" format introduced with AES 3.30/TOS 4 (rsh_vrsn bit 2 set), which
// appends an extension array plus colour icon (CICONBLK) data after
// rsh_rssize.
#include "rsc/rsc.h"
#include "base/bytes.h"
#include <algorithm>
#include <cstring>
#include <map>

namespace rsc {

using base::rd16;
using base::rd32;
using base::rds16;

namespace {

constexpr uint16_t NEW_FORMAT = 0x0004;
constexpr int HDR_SIZE = 36, OBJ_SIZE = 24, TED_SIZE = 28, IB_SIZE = 34, BB_SIZE = 14, CICON_SIZE = 22;

struct Reader {
    const std::vector<uint8_t>& d;
    LoadResult& res;
    explicit Reader(const std::vector<uint8_t>& data, LoadResult& r) : d(data), res(r) {}

    bool inRange(uint32_t off, uint32_t len) const { return off <= d.size() && len <= d.size() - off; }

    std::string str(uint32_t off)
    {
        std::string s;
        if (off == 0 || off >= d.size()) return s;
        while (off < d.size() && d[off]) s += char(d[off++]);
        return s;
    }

    std::vector<uint16_t> words(uint32_t off, size_t n)
    {
        std::vector<uint16_t> v(n, 0);
        if (!inRange(off, uint32_t(n * 2))) {
            res.warnings.push_back("image data out of range");
            return v;
        }
        for (size_t i = 0; i < n; i++) v[i] = rd16(d, off + 2 * i);
        return v;
    }

    TedInfo ted(uint32_t off)
    {
        TedInfo t;
        if (!inRange(off, TED_SIZE)) { res.warnings.push_back("TEDINFO out of range"); return t; }
        t.text = str(rd32(d, off));
        t.tmplt = str(rd32(d, off + 4));
        t.valid = str(rd32(d, off + 8));
        t.font = rds16(d, off + 12);
        t.fontid = rds16(d, off + 14);
        t.just = rds16(d, off + 16);
        t.color = rd16(d, off + 18);
        t.fontsize = rds16(d, off + 20);
        t.thickness = rds16(d, off + 22);
        t.txtlen = rds16(d, off + 24);
        t.tmplen = rds16(d, off + 26);
        return t;
    }

    void iconHeader(uint32_t off, IconBlk& ib)
    {
        ib.ch = rd16(d, off + 12);
        ib.xchar = rds16(d, off + 14); ib.ychar = rds16(d, off + 16);
        ib.xicon = rds16(d, off + 18); ib.yicon = rds16(d, off + 20);
        ib.wicon = rds16(d, off + 22); ib.hicon = rds16(d, off + 24);
        ib.xtext = rds16(d, off + 26); ib.ytext = rds16(d, off + 28);
        ib.wtext = rds16(d, off + 30); ib.htext = rds16(d, off + 32);
        if (ib.wicon < 0) ib.wicon = 0;
        if (ib.hicon < 0) ib.hicon = 0;
    }

    IconBlk icon(uint32_t off)
    {
        IconBlk ib;
        if (!inRange(off, IB_SIZE)) { res.warnings.push_back("ICONBLK out of range"); return ib; }
        iconHeader(off, ib);
        int w = (ib.wicon + 15) & ~15, h = ib.hicon;
        ib.mask.w = ib.data.w = w;
        ib.mask.h = ib.data.h = h;
        ib.mask.words = words(rd32(d, off), size_t(w / 16) * h);
        ib.data.words = words(rd32(d, off + 4), size_t(w / 16) * h);
        ib.text = str(rd32(d, off + 8));
        return ib;
    }

    BitBlk bitblk(uint32_t off)
    {
        BitBlk b;
        if (!inRange(off, BB_SIZE)) { res.warnings.push_back("BITBLK out of range"); return b; }
        int wb = rds16(d, off + 4), hl = rds16(d, off + 6);
        if (wb < 0) wb = 0;
        if (hl < 0) hl = 0;
        wb &= ~1;  // bytes per line is always even (word aligned)
        b.x = rds16(d, off + 8);
        b.y = rds16(d, off + 10);
        b.color = rds16(d, off + 12);
        b.img.w = wb * 8;
        b.img.h = hl;
        b.img.words = words(rd32(d, off), size_t(wb / 2) * hl);
        return b;
    }
};

} // namespace

LoadResult loadRsc(const std::vector<uint8_t>& d, Rsc& out)
{
    LoadResult res;
    out = Rsc();
    if (d.size() < HDR_SIZE) { res.error = "file too small to be a GEM resource"; return res; }
    Reader rd(d, res);

    uint16_t vrsn = rd16(d, 0);
    uint16_t o_object = rd16(d, 2), o_frstr = rd16(d, 10), o_frimg = rd16(d, 16), o_trindex = rd16(d, 18);
    uint16_t nobs = rd16(d, 20), ntree = rd16(d, 22), nstring = rd16(d, 30), nimages = rd16(d, 32);
    uint16_t rssize = rd16(d, 34);

    if ((vrsn & ~NEW_FORMAT) > 3)
        res.warnings.push_back("unusual rsh_vrsn value " + std::to_string(vrsn));
    if (!(vrsn & NEW_FORMAT) && rssize != d.size() && rssize > d.size()) {
        res.error = "rsh_rssize is larger than the file: not a valid GEM resource";
        return res;
    }
    if (!rd.inRange(o_object, uint32_t(nobs) * OBJ_SIZE) || !rd.inRange(o_trindex, uint32_t(ntree) * 4)) {
        res.error = "object or tree table outside of file: not a valid GEM resource";
        return res;
    }

    // colour icon table
    std::vector<uint32_t> ciconOffsets;
    if (vrsn & NEW_FORMAT) {
        uint32_t ext = rssize;
        if (rd.inRange(ext, 8)) {
            uint32_t ctab = rd32(d, ext + 4);
            if (ctab != 0 && ctab != 0xFFFFFFFFu && ctab < d.size()) {
                uint32_t p = ctab;
                size_t n = 0;
                while (rd.inRange(p, 4) && rd32(d, p) != 0xFFFFFFFFu && n < 10000) { p += 4; n++; }
                p += 4;  // skip -1
                for (size_t i = 0; i < n; i++) {
                    if (!rd.inRange(p, IB_SIZE + 4)) { res.warnings.push_back("truncated colour icon data"); break; }
                    ciconOffsets.push_back(p);
                    IconBlk ib; rd.iconHeader(p, ib);
                    uint32_t nres = rd32(d, p + IB_SIZE);
                    uint32_t monoBytes = uint32_t(((ib.wicon + 15) / 16) * 2 * ib.hicon);
                    p += IB_SIZE + 4 + 2 * monoBytes + 12;
                    for (uint32_t r = 0; r < nres && r < 64; r++) {
                        if (!rd.inRange(p, CICON_SIZE)) break;
                        int planes = rd16(d, p);
                        bool sel = rd32(d, p + 10) != 0;
                        p += CICON_SIZE + monoBytes * planes + monoBytes;
                        if (sel) p += monoBytes * planes + monoBytes;
                    }
                }
            }
        }
    }

    auto readCicon = [&](uint32_t idx, Object& o) {
        if (idx >= ciconOffsets.size()) {
            res.warnings.push_back("G_CICON references missing colour icon " + std::to_string(idx));
            o.icon = makeObject(G_ICON, 8, 16).icon;
            return;
        }
        uint32_t p = ciconOffsets[idx];
        IconBlk& ib = o.icon;
        rd.iconHeader(p, ib);
        int w = (ib.wicon + 15) & ~15, h = ib.hicon, wpr = w / 16;
        size_t mw = size_t(wpr) * h;
        uint32_t nres = rd32(d, p + IB_SIZE);
        p += IB_SIZE + 4;
        ib.data.w = ib.mask.w = w; ib.data.h = ib.mask.h = h;
        ib.data.words = rd.words(p, mw); p += uint32_t(mw * 2);
        ib.mask.words = rd.words(p, mw); p += uint32_t(mw * 2);
        char txt[13] = { 0 };
        for (int i = 0; i < 12 && p + i < d.size(); i++) txt[i] = char(d[p + i]);
        ib.text = txt;
        p += 12;
        for (uint32_t r = 0; r < nres && r < 64; r++) {
            CIcon ci;
            ci.planes = rd16(d, p);
            if (ci.planes < 1 || ci.planes > 8) { res.warnings.push_back("colour icon with unsupported plane count"); break; }
            ci.hasSel = rd32(d, p + 10) != 0;
            p += CICON_SIZE;
            ci.col = rd.words(p, mw * ci.planes); p += uint32_t(mw * 2 * ci.planes);
            ci.colMask = rd.words(p, mw); p += uint32_t(mw * 2);
            if (ci.hasSel) {
                ci.sel = rd.words(p, mw * ci.planes); p += uint32_t(mw * 2 * ci.planes);
                ci.selMask = rd.words(p, mw); p += uint32_t(mw * 2);
            }
            o.cicons.push_back(std::move(ci));
        }
    };

    auto readObject = [&](uint32_t off, Object& o) {
        o.next = rds16(d, off); o.head = rds16(d, off + 2); o.tail = rds16(d, off + 4);
        o.type = rd16(d, off + 6); o.flags = rd16(d, off + 8); o.state = rd16(d, off + 10);
        uint32_t spec = rd32(d, off + 12);
        o.x = rds16(d, off + 16); o.y = rds16(d, off + 18); o.w = rds16(d, off + 20); o.h = rds16(d, off + 22);
        o.spec = spec;
        switch (o.baseType()) {
        case G_BOX: case G_IBOX: case G_BOXCHAR: case G_USERDEF:
            break;
        case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT:
            o.ted = rd.ted(spec); break;
        case G_STRING: case G_BUTTON: case G_TITLE: case G_SHORTCUT: case G_WINTITLE:
            o.str = rd.str(spec); break;
        case G_ICON: o.icon = rd.icon(spec); break;
        case G_CICON: readCicon(spec, o); break;
        case G_IMAGE: o.bit = rd.bitblk(spec); break;
        default: break;   // unknown/extended: keep raw spec
        }
    };

    for (unsigned t = 0; t < ntree; t++) {
        uint32_t rootOff = rd32(d, o_trindex + 4 * t);
        Tree tree;
        if (rootOff < o_object || (rootOff - o_object) % OBJ_SIZE) {
            res.warnings.push_back("tree " + std::to_string(t) + " has an invalid root offset");
            tree.obj.push_back(makeObject(G_BOX, 8, 16));
            out.trees.push_back(tree);
            continue;
        }
        uint32_t root = (rootOff - o_object) / OBJ_SIZE;
        for (uint32_t i = root; i < nobs; i++) {
            Object o;
            readObject(o_object + i * OBJ_SIZE, o);
            bool last = o.flags & LASTOB;
            tree.obj.push_back(std::move(o));
            if (last) break;
        }
        // sanitize links
        int n = (int)tree.obj.size();
        for (auto& o : tree.obj) {
            if (o.next >= n) o.next = -1;
            if (o.head >= n || o.tail >= n) o.head = o.tail = -1;
        }
        std::string v = validateTree(tree);
        if (!v.empty()) res.warnings.push_back("tree " + std::to_string(t) + ": " + v);
        MenuInfo mi;
        tree.kind = analyseMenu(tree, mi) ? TreeKind::Menu : TreeKind::Dialog;
        out.trees.push_back(std::move(tree));
    }

    for (unsigned i = 0; i < nstring; i++) {
        FreeString fs;
        fs.text = rd.str(rd32(d, o_frstr + 4 * i));
        out.strings.push_back(fs);
    }
    for (unsigned i = 0; i < nimages; i++) {
        FreeImage fi;
        fi.bit = rd.bitblk(rd32(d, o_frimg + 4 * i));
        out.images.push_back(fi);
    }
    res.ok = true;
    return res;
}

LoadResult loadRscFile(const std::string& path, Rsc& out, bool loadNames)
{
    LoadResult res;
    std::vector<uint8_t> d;
    if (!base::readFile(path, d)) { res.error = "cannot read " + path; return res; }
    res = loadRsc(d, out);
    if (!res.ok || !loadNames) return res;
    std::string f;
    std::vector<uint8_t> nd;
    if (!(f = base::findSibling(path, "hrd")).empty() && base::readFile(f, nd) && loadNamesHrd(nd, out)) return res;
    if (!(f = base::findSibling(path, "def")).empty() && base::readFile(f, nd) && loadNamesDef(nd, out)) return res;
    if (!(f = base::findSibling(path, "rsd")).empty() && base::readFile(f, nd) && loadNamesDef(nd, out)) return res;
    if (!(f = base::findSibling(path, "dfn")).empty() && base::readFile(f, nd) && loadNamesDfn(nd, out)) return res;
    if (!(f = base::findSibling(path, "h")).empty() && base::readFile(f, nd)) {
        loadNamesH(std::string(nd.begin(), nd.end()), out);
    }
    return res;
}

// ================================================================ writer

bool saveRsc(const Rsc& r, std::vector<uint8_t>& out, std::string& err, const SaveOptions& opt)
{
    base::Writer w;
    for (int i = 0; i < HDR_SIZE; i++) w.u8(0);

    struct ObjRefs { uint32_t s1 = 0, s2 = 0, s3 = 0; uint32_t img1 = 0, img2 = 0; int sub = -1; };
    std::vector<std::vector<ObjRefs>> refs(r.trees.size());
    std::vector<uint32_t> frstrOff(r.strings.size());

    auto editLen = [](const TedInfo& t) {
        int n = 0;
        for (char c : t.tmplt) if (c == '_') n++;
        return n;
    };
    // te_txtlen: keep the value from the file (programs rely on it as the
    // edit buffer size); for new objects derive it from the template.
    auto tedText = [](const TedInfo& t) { return t.text; };
    auto txtLen = [&](const TedInfo& t) {
        int n = std::max<int>(t.txtlen, int(t.text.size()) + 1);
        if (t.txtlen <= 0) n = std::max(n, editLen(t) + 1);
        return n;
    };

    // ---- strings
    for (size_t t = 0; t < r.trees.size(); t++) {
        refs[t].resize(r.trees[t].obj.size());
        for (size_t i = 0; i < r.trees[t].obj.size(); i++) {
            const Object& o = r.trees[t].obj[i];
            ObjRefs& rf = refs[t][i];
            int bt = o.baseType();
            if (typeHasTedinfo(bt)) {
                std::string txt = tedText(o.ted);
                rf.s1 = uint32_t(w.pos()); w.str0(txt);
                rf.s2 = uint32_t(w.pos()); w.str0(o.ted.tmplt);
                rf.s3 = uint32_t(w.pos()); w.str0(o.ted.valid);
            } else if (bt == G_STRING || bt == G_BUTTON || bt == G_TITLE || bt == G_SHORTCUT || bt == G_WINTITLE) {
                rf.s1 = uint32_t(w.pos()); w.str0(o.str);
            } else if (bt == G_ICON) {
                rf.s1 = uint32_t(w.pos()); w.str0(o.icon.text);
            }
        }
    }
    for (size_t i = 0; i < r.strings.size(); i++) { frstrOff[i] = uint32_t(w.pos()); w.str0(r.strings[i].text); }
    w.align2();

    // ---- image data
    uint32_t imdata = uint32_t(w.pos());
    auto putWords = [&](const std::vector<uint16_t>& v, size_t n) {
        uint32_t o = uint32_t(w.pos());
        for (size_t i = 0; i < n; i++) w.u16(i < v.size() ? v[i] : 0);
        return o;
    };
    std::vector<uint32_t> frimgData(r.images.size());
    for (size_t t = 0; t < r.trees.size(); t++)
        for (size_t i = 0; i < r.trees[t].obj.size(); i++) {
            const Object& o = r.trees[t].obj[i];
            ObjRefs& rf = refs[t][i];
            if (o.baseType() == G_ICON) {
                size_t n = size_t((o.icon.wicon + 15) / 16) * o.icon.hicon;
                rf.img1 = putWords(o.icon.mask.words, n);
                rf.img2 = putWords(o.icon.data.words, n);
            } else if (o.baseType() == G_IMAGE) {
                rf.img1 = putWords(o.bit.img.words, size_t(o.bit.img.wpr()) * o.bit.img.h);
            }
        }
    for (size_t i = 0; i < r.images.size(); i++)
        frimgData[i] = putWords(r.images[i].bit.img.words, size_t(r.images[i].bit.img.wpr()) * r.images[i].bit.img.h);

    // ---- free string table
    uint32_t o_frstr = uint32_t(w.pos());
    for (uint32_t off : frstrOff) w.u32(off);

    // ---- BITBLKs
    uint32_t o_bitblk = uint32_t(w.pos());
    int nbb = 0;
    auto putBitblk = [&](const BitBlk& b, uint32_t data) {
        w.u32(data); w.u16(uint16_t(b.img.wpr() * 2)); w.u16(uint16_t(b.img.h));
        w.u16(uint16_t(b.x)); w.u16(uint16_t(b.y)); w.u16(uint16_t(b.color));
        nbb++;
    };
    for (size_t t = 0; t < r.trees.size(); t++)
        for (size_t i = 0; i < r.trees[t].obj.size(); i++)
            if (r.trees[t].obj[i].baseType() == G_IMAGE) {
                refs[t][i].sub = int(w.pos());
                putBitblk(r.trees[t].obj[i].bit, refs[t][i].img1);
            }
    std::vector<uint32_t> frimgOff(r.images.size());
    for (size_t i = 0; i < r.images.size(); i++) { frimgOff[i] = uint32_t(w.pos()); putBitblk(r.images[i].bit, frimgData[i]); }

    // ---- ICONBLKs
    uint32_t o_iconblk = uint32_t(w.pos());
    int nib = 0;
    for (size_t t = 0; t < r.trees.size(); t++)
        for (size_t i = 0; i < r.trees[t].obj.size(); i++) {
            const Object& o = r.trees[t].obj[i];
            if (o.baseType() != G_ICON) continue;
            refs[t][i].sub = int(w.pos());
            const IconBlk& ib = o.icon;
            w.u32(refs[t][i].img1); w.u32(refs[t][i].img2); w.u32(refs[t][i].s1);
            w.u16(ib.ch); w.u16(uint16_t(ib.xchar)); w.u16(uint16_t(ib.ychar));
            w.u16(uint16_t(ib.xicon)); w.u16(uint16_t(ib.yicon)); w.u16(uint16_t(ib.wicon)); w.u16(uint16_t(ib.hicon));
            w.u16(uint16_t(ib.xtext)); w.u16(uint16_t(ib.ytext)); w.u16(uint16_t(ib.wtext)); w.u16(uint16_t(ib.htext));
            nib++;
        }

    // ---- TEDINFOs
    uint32_t o_tedinfo = uint32_t(w.pos());
    int nted = 0;
    for (size_t t = 0; t < r.trees.size(); t++)
        for (size_t i = 0; i < r.trees[t].obj.size(); i++) {
            const Object& o = r.trees[t].obj[i];
            if (!typeHasTedinfo(o.baseType())) continue;
            refs[t][i].sub = int(w.pos());
            const TedInfo& te = o.ted;
            w.u32(refs[t][i].s1); w.u32(refs[t][i].s2); w.u32(refs[t][i].s3);
            w.u16(uint16_t(te.font)); w.u16(uint16_t(te.fontid)); w.u16(uint16_t(te.just)); w.u16(te.color);
            w.u16(uint16_t(te.fontsize)); w.u16(uint16_t(te.thickness));
            w.u16(uint16_t(txtLen(te))); w.u16(uint16_t(te.tmplt.size() + 1));
            nted++;
        }

    // ---- colour icon indices
    int ciconIndex = 0;
    std::vector<const Object*> cicons;

    // ---- OBJECTs
    uint32_t o_object = uint32_t(w.pos());
    std::vector<uint32_t> roots;
    int nobs = 0;
    for (size_t t = 0; t < r.trees.size(); t++) {
        const Tree& tree = r.trees[t];
        roots.push_back(uint32_t(w.pos()));
        if (tree.obj.empty()) {
            err = "tree " + std::to_string(t) + " is empty";
            return false;
        }
        for (size_t i = 0; i < tree.obj.size(); i++) {
            const Object& o = tree.obj[i];
            uint32_t spec = o.spec;
            switch (o.baseType()) {
            case G_BOX: case G_IBOX: case G_BOXCHAR: case G_USERDEF: break;
            case G_CICON: spec = uint32_t(ciconIndex++); cicons.push_back(&o); break;
            case G_STRING: case G_BUTTON: case G_TITLE: case G_SHORTCUT: case G_WINTITLE: spec = refs[t][i].s1; break;
            default:
                if (refs[t][i].sub >= 0) spec = uint32_t(refs[t][i].sub);
                break;
            }
            uint16_t flags = o.flags & ~LASTOB;
            if (i + 1 == tree.obj.size()) flags |= LASTOB;
            w.u16(uint16_t(o.next)); w.u16(uint16_t(o.head)); w.u16(uint16_t(o.tail));
            w.u16(o.type); w.u16(flags); w.u16(o.state); w.u32(spec);
            w.u16(uint16_t(o.x)); w.u16(uint16_t(o.y)); w.u16(uint16_t(o.w)); w.u16(uint16_t(o.h));
            nobs++;
        }
    }

    // ---- tree index & free images
    uint32_t o_trindex = uint32_t(w.pos());
    for (uint32_t off : roots) w.u32(off);
    uint32_t o_frimg = uint32_t(w.pos());
    for (uint32_t off : frimgOff) w.u32(off);

    uint32_t rssize = uint32_t(w.pos());
    if (rssize > 0xFFFF) {
        err = "resource too large: " + std::to_string(rssize) + " bytes (GEM resources are limited to 64KB "
              "excluding colour icons)";
        return false;
    }

    bool ext = !cicons.empty() || opt.forceExtended;
    uint16_t vrsn = ext ? NEW_FORMAT : 0x0001;

    if (ext) {
        size_t extPos = w.pos();
        w.u32(0);              // file size (patched below)
        w.u32(0xFFFFFFFFu);    // CICON table offset
        w.u32(0);              // end of extension array
        if (!cicons.empty()) {
            w.put32(extPos + 4, uint32_t(w.pos()));
            for (size_t i = 0; i < cicons.size(); i++) w.u32(0);
            w.u32(0xFFFFFFFFu);
            for (const Object* op : cicons) {
                const IconBlk& ib = op->icon;
                int wpr = (ib.wicon + 15) / 16;
                size_t mw = size_t(wpr) * ib.hicon;
                w.u32(0); w.u32(0); w.u32(0);
                w.u16(ib.ch); w.u16(uint16_t(ib.xchar)); w.u16(uint16_t(ib.ychar));
                w.u16(uint16_t(ib.xicon)); w.u16(uint16_t(ib.yicon)); w.u16(uint16_t(ib.wicon)); w.u16(uint16_t(ib.hicon));
                w.u16(uint16_t(ib.xtext)); w.u16(uint16_t(ib.ytext)); w.u16(uint16_t(ib.wtext)); w.u16(uint16_t(ib.htext));
                w.u32(uint32_t(op->cicons.size()));
                putWords(ib.data.words, mw);
                putWords(ib.mask.words, mw);
                char txt[12] = { 0 };
                strncpy(txt, ib.text.c_str(), 11);
                w.bytes(txt, 12);
                for (size_t k = 0; k < op->cicons.size(); k++) {
                    const CIcon& c = op->cicons[k];
                    w.u16(uint16_t(c.planes));
                    w.u32(0); w.u32(0);
                    w.u32(c.hasSel ? 1u : 0u); w.u32(0);
                    w.u32(k + 1 < op->cicons.size() ? 1u : 0u);
                    putWords(c.col, mw * c.planes);
                    putWords(c.colMask, mw);
                    if (c.hasSel) { putWords(c.sel, mw * c.planes); putWords(c.selMask, mw); }
                }
            }
        }
        w.put32(extPos, uint32_t(w.pos()));
    }

    // ---- header
    w.put16(0, vrsn);
    w.put16(2, uint16_t(o_object));
    w.put16(4, uint16_t(o_tedinfo));
    w.put16(6, uint16_t(o_iconblk));
    w.put16(8, uint16_t(o_bitblk));
    w.put16(10, uint16_t(o_frstr));
    w.put16(12, uint16_t(HDR_SIZE));
    w.put16(14, uint16_t(imdata));
    w.put16(16, uint16_t(o_frimg));
    w.put16(18, uint16_t(o_trindex));
    w.put16(20, uint16_t(nobs));
    w.put16(22, uint16_t(r.trees.size()));
    w.put16(24, uint16_t(nted));
    w.put16(26, uint16_t(nib));
    w.put16(28, uint16_t(nbb));
    w.put16(30, uint16_t(r.strings.size()));
    w.put16(32, uint16_t(r.images.size()));
    w.put16(34, uint16_t(rssize));
    out.swap(w.d);
    return true;
}

} // namespace rsc
