// In-memory model of an Atari GEM resource (RSC) file.
//
// Object trees are kept exactly like GEM keeps them: a flat array of
// OBJECTs linked by ob_next/ob_head/ob_tail.  This preserves object
// indices from loaded files (programs depend on them) and lets all the
// editing operations mirror the AES objc_add/objc_delete semantics.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rsc {

// ------------------------------------------------------------ constants
enum : int {
    G_BOX = 20, G_TEXT = 21, G_BOXTEXT = 22, G_IMAGE = 23, G_USERDEF = 24,
    G_IBOX = 25, G_BUTTON = 26, G_BOXCHAR = 27, G_STRING = 28, G_FTEXT = 29,
    G_FBOXTEXT = 30, G_ICON = 31, G_TITLE = 32, G_CICON = 33,
    // MagiC / later AES extended types (stored and drawn as best possible)
    G_SWBUTTON = 34, G_POPUP = 35, G_WINTITLE = 36, G_EDIT = 37, G_SHORTCUT = 38,
};

enum : uint16_t {
    NONE = 0, SELECTABLE = 0x0001, DEFAULT = 0x0002, EXIT = 0x0004, EDITABLE = 0x0008,
    RBUTTON = 0x0010, LASTOB = 0x0020, TOUCHEXIT = 0x0040, HIDETREE = 0x0080,
    INDIRECT = 0x0100, FL3DIND = 0x0200, FL3DBAK = 0x0400, FL3DACT = 0x0600,
    FL3DMASK = 0x0600, SUBMENU = 0x0800,
};

enum : uint16_t {
    NORMAL = 0, SELECTED = 0x01, CROSSED = 0x02, CHECKED = 0x04, DISABLED = 0x08,
    OUTLINED = 0x10, SHADOWED = 0x20, WHITEBAK = 0x40, DRAW3D = 0x80,
};

enum { TE_LEFT = 0, TE_RIGHT = 1, TE_CNTR = 2 };
enum { GDOS_PROP = 0, GDOS_MONO = 1, GDOS_BITM = 2, FONT_IBM = 3, FONT_SMALL = 5 };

const char* typeName(int type);          // "G_BOX"...
const char* shortTypeName(int type);     // "BOX"...
bool typeHasTedinfo(int type);
bool typeHasString(int type);            // G_STRING, G_BUTTON, G_TITLE (+ ext)
bool typeHasBoxSpec(int type);           // G_BOX, G_IBOX, G_BOXCHAR

// ------------------------------------------------------------ bitmaps
// A monochrome bitmap stored as 16-bit words, MSB = leftmost pixel.
struct Bitmap {
    int w = 0, h = 0;                  // w is always a multiple of 16
    std::vector<uint16_t> words;       // h * (w/16)
    int wpr() const { return w / 16; }
    void resize(int nw, int nh);       // keeps content (top-left anchored)
    bool get(int x, int y) const;
    void set(int x, int y, bool v);
    bool operator==(const Bitmap& o) const { return w == o.w && h == o.h && words == o.words; }
};

struct TedInfo {
    std::string text, tmplt, valid;
    int16_t font = FONT_IBM;
    int16_t fontid = 0;      // te_fontid (AES 4.1 / GDOS), te_junk1 in old AES
    int16_t just = TE_LEFT;
    uint16_t color = 0x1180;
    int16_t fontsize = 0;    // te_fontsize, te_junk2 in old AES
    int16_t thickness = 0;
    int16_t txtlen = 0;      // as found in file (AES recomputes on load)
    int16_t tmplen = 0;
};

struct IconBlk {
    Bitmap mask, data;
    std::string text;
    uint16_t ch = 0x1000;            // ib_char: fg(15-12) bg(11-8) char(7-0)
    int16_t xchar = 0, ychar = 0;
    int16_t xicon = 0, yicon = 0, wicon = 32, hicon = 32;
    int16_t xtext = 0, ytext = 32, wtext = 72, htext = 8;
};

// One resolution of a colour icon.  Planes are stored device-independent
// ("standard format"): plane 0 completely, then plane 1, ...  Pixel values
// are hardware pixel values, exactly as stored in the RSC file.
struct CIcon {
    int planes = 4;
    std::vector<uint16_t> col, colMask;   // planes*wpr*h, wpr*h
    bool hasSel = false;
    std::vector<uint16_t> sel, selMask;
    int pixel(const std::vector<uint16_t>& d, int wpr, int h, int x, int y) const;
    void setPixel(std::vector<uint16_t>& d, int wpr, int h, int x, int y, int v) const;
};

struct BitBlk {
    Bitmap img;                  // bi_wb*8 x bi_hl
    int16_t x = 0, y = 0;        // bi_x, bi_y (source offsets)
    int16_t color = 1;
};

// ------------------------------------------------------------ objects
struct Object {
    int16_t next = -1, head = -1, tail = -1;
    uint16_t type = G_BOX;       // low byte: type, high byte: extended type
    uint16_t flags = 0;
    uint16_t state = 0;
    // ob_x..ob_height as stored in the file: low byte = characters,
    // high byte = pixel offset (signed, ">128 is negative").
    int16_t x = 0, y = 0, w = 0, h = 0;

    // ob_spec, depending on type:
    uint32_t spec = 0x00FF1100;  // BOX/IBOX/BOXCHAR raw spec, USERDEF raw
    std::string str;             // STRING/BUTTON/TITLE text
    TedInfo ted;                 // TEXT/BOXTEXT/FTEXT/FBOXTEXT
    IconBlk icon;                // ICON, and mono part of CICON
    std::vector<CIcon> cicons;   // CICON resolutions
    BitBlk bit;                  // IMAGE

    std::string name;            // symbolic name (from/to .H/.HRD/...)

    int baseType() const { return type & 0xFF; }
    int extType() const { return type >> 8; }
};

enum class TreeKind { Unknown = 0, Dialog = 1, Menu = 2, Panel = 3 };

struct Tree {
    std::string name;
    TreeKind kind = TreeKind::Dialog;
    std::vector<Object> obj;
};

struct FreeString {
    std::string name;
    std::string text;
    bool isAlert() const { return text.size() > 4 && text[0] == '[' && text[2] == ']' && text[3] == '['; }
};

struct FreeImage {
    std::string name;
    BitBlk bit;
};

struct Rsc {
    std::vector<Tree> trees;
    std::vector<FreeString> strings;
    std::vector<FreeImage> images;
    std::string prefix;          // name prefix (HRD)
    bool hasColourIcons() const;
};

// ------------------------------------------------------------ coordinates
// Convert a packed RSC coordinate (chars + pixel offset) to pixels.
int toPixels(int16_t raw, int cell, bool isWidth = false, int screenWidth = 0);
// Convert pixels back to packed char/pixel form for the given cell size.
int16_t fromPixels(int px, int cell);
inline int rawChars(int16_t raw) { return raw & 0xFF; }
inline int rawPixOff(int16_t raw) { int o = (raw >> 8) & 0xFF; return o > 128 ? o - 256 : o; }
inline int16_t makeRaw(int chars, int pix) { return int16_t((chars & 0xFF) | ((pix & 0xFF) << 8)); }

// ------------------------------------------------------------ tree ops
// All of these operate on GEM links and keep LASTOB correct.
int parentOf(const Tree& t, int obj);
std::vector<int> childrenOf(const Tree& t, int obj);
bool isAncestor(const Tree& t, int anc, int obj);     // anc is (strict) ancestor of obj
int depthOf(const Tree& t, int obj);
void fixLastOb(Tree& t);
// Append a new object as last child of parent; returns its index.
int addObject(Tree& t, int parent, const Object& o);
// Insert a copy of 'src' subtree (root srcRoot) under parent; returns new root index.
int copySubtree(Tree& dst, int parent, const Tree& src, int srcRoot);
// Delete an object and its subtree, renumbering the remaining objects.
void deleteObject(Tree& t, int obj);
// Move obj to position pos among its siblings (0 = first = drawn first).
void reorderObject(Tree& t, int obj, int pos);
// Re-parent obj (keeps absolute position computed by caller).
void reparentObject(Tree& t, int obj, int newParent);
// Rebuild the array in depth-first order (optional tidy-up).
void sortTree(Tree& t);
// Validate links; returns empty string when OK.
std::string validateTree(const Tree& t);

// ------------------------------------------------------------ factories
Object makeObject(int type, int cellW, int cellH);
Tree makeDialogTree(const std::string& name, int cellW, int cellH);
Tree makeMenuTree(const std::string& name);
// Menu helpers (standard GEM menu tree layout)
struct MenuInfo { int bar = -1, activeBox = -1, screen = -1; std::vector<int> titles, boxes; };
bool analyseMenu(const Tree& t, MenuInfo& mi);
int menuAddTitle(Tree& t, const std::string& text);
int menuAddItem(Tree& t, int box, const std::string& text, bool separator);
void menuRelayout(Tree& t, int cellW, int cellH);

// ------------------------------------------------------------ I/O
struct LoadResult { bool ok = false; std::string error; std::vector<std::string> warnings; };
LoadResult loadRsc(const std::vector<uint8_t>& data, Rsc& out);
LoadResult loadRscFile(const std::string& path, Rsc& out, bool loadNames = true);

struct SaveOptions {
    bool forceExtended = false;   // write extension array even without CICONs
};
bool saveRsc(const Rsc& r, std::vector<uint8_t>& out, std::string& err, const SaveOptions& opt = {});

// Name files.  load* apply names to r; return false if unreadable.
bool loadNamesHrd(const std::vector<uint8_t>& d, Rsc& r);
bool loadNamesDef(const std::vector<uint8_t>& d, Rsc& r);   // .DEF / .RSD (RCS 1.x)
bool loadNamesDfn(const std::vector<uint8_t>& d, Rsc& r);   // .DFN (RCS 2.x)
bool loadNamesH(const std::string& text, Rsc& r);           // C header
std::vector<uint8_t> saveNamesHrd(const Rsc& r);
std::vector<uint8_t> saveNamesDef(const Rsc& r);

// Source exports
enum class HeaderLang { C, Pascal, Gfa, Asm, Modula };
std::string exportHeader(const Rsc& r, const std::string& rscName, HeaderLang lang);
std::string exportRsh(const Rsc& r, const std::string& rscName);  // C source with embedded resource
std::string makeIdentifier(const std::string& s);
// A demonstration resource using every object type.
Rsc makeExample();
// Assign default names to unnamed trees/strings/images (TREE01, ...).
void ensureTreeNames(Rsc& r);
// Returns list of duplicate/invalid names.
std::vector<std::string> checkNames(const Rsc& r);

} // namespace rsc
