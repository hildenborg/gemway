#!/bin/sh
# GEMWay self tests - run with "make test".  No display is needed.
set -e
BIN=${1:-./gemway}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

echo "== example resource (all object types, menu, 3D, alerts, colour icon)"
"$BIN" --make-example "$T/demo.rsc" || fail "make-example"

echo "== round trip: load + save must be byte identical"
"$BIN" --convert "$T/demo.rsc" "$T/round.rsc" || fail "convert"
cmp "$T/demo.rsc" "$T/round.rsc" || fail "round trip differs"

echo "== names are restored from the .HRD file"
"$BIN" --dump "$T/round.rsc" > "$T/dump.txt" || fail "dump"
for n in MAINMENU ALLTYPES DIALOG3D BT_OK ED_NAME IC_COLOUR AL_QUIT IMG_LOGO; do
    grep -q "$n" "$T/dump.txt" || fail "name $n missing"
done
grep -q "colour icons" "$T/dump.txt" || fail "colour icons not detected"

echo "== render every item in every AES version and screen mode"
for aes in 0 1 2 3 4; do
    for res in 0 1 2 3 4 5 6 7 8; do
        for item in 0 1 2 s0 s1 s2 s3 i0; do
            "$BIN" --render "$T/demo.rsc" $item "$T/r.png" --aes $aes --res $res || fail "render $item aes=$aes res=$res"
        done
    done
done
"$BIN" --render "$T/demo.rsc" 0 "$T/menu.png" --menu 1 || fail "menu dropdown render"
"$BIN" --alert "[3][Line one|Line two][ OK |Cancel]" "$T/alert.png" || fail "alert render"

echo "== deterministic rendering"
"$BIN" --render "$T/demo.rsc" 1 "$T/a.png" --aes 3 --res 7
"$BIN" --render "$T/round.rsc" 1 "$T/b.png" --aes 3 --res 7
cmp "$T/a.png" "$T/b.png" || fail "renders of identical resources differ"

echo "== header and source exports"
for l in c pascal gfa asm modula rsh; do
    "$BIN" --header "$T/demo.rsc" $l > "$T/out.$l" || fail "export $l"
    test -s "$T/out.$l" || fail "export $l empty"
done
grep -q "#define BT_OK" "$T/out.c" || fail "C header content"

if command -v cc >/dev/null 2>&1; then
    echo "== exported C header and RSH source compile"
    cat > "$T/portab.h" <<'H'
typedef short WORD; typedef unsigned short UWORD; typedef char BYTE; typedef long LONG;
H
    cat > "$T/aes.h" <<'H'
typedef struct { LONG te_ptext, te_ptmplt, te_pvalid; WORD te_font, te_fontid, te_just, te_color, te_fontsize, te_thickness, te_txtlen, te_tmplen; } TEDINFO;
typedef struct { LONG ib_pmask, ib_pdata, ib_ptext; WORD ib_char, ib_xchar, ib_ychar, ib_xicon, ib_yicon, ib_wicon, ib_hicon, ib_xtext, ib_ytext, ib_wtext, ib_htext; } ICONBLK;
typedef struct { LONG bi_pdata; WORD bi_wb, bi_hl, bi_x, bi_y, bi_color; } BITBLK;
typedef struct { WORD ob_next, ob_head, ob_tail; UWORD ob_type, ob_flags, ob_state; LONG ob_spec; WORD ob_x, ob_y, ob_width, ob_height; } OBJECT;
enum { G_BOX=20, G_TEXT, G_BOXTEXT, G_IMAGE, G_USERDEF, G_IBOX, G_BUTTON, G_BOXCHAR, G_STRING, G_FTEXT, G_FBOXTEXT, G_ICON, G_TITLE, G_CICON };
enum { NONE=0, SELECTABLE=1, DEFAULT=2, EXIT=4, EDITABLE=8, RBUTTON=16, LASTOB=32, TOUCHEXIT=64, HIDETREE=128, INDIRECT=256 };
enum { NORMAL=0, SELECTED=1, CROSSED=2, CHECKED=4, DISABLED=8, OUTLINED=16, SHADOWED=32 };
enum { IBM=3, SMALL=5, TE_LEFT=0, TE_RIGHT=1, TE_CNTR=2 };
H
    cp "$T/out.c" "$T/DEMO.H"
    cp "$T/out.rsh" "$T/demo.c"
    printf '#include "DEMO.H"\nint x[] = { MAINMENU, ALLTYPES, BT_OK, AL_QUIT, IMG_LOGO };\n' > "$T/use.c"
    cc -fsyntax-only -I"$T" "$T/use.c" || fail "C header does not compile"
    cc -fsyntax-only -w -I"$T" "$T/demo.c" || fail "RSH source does not compile"
fi

echo "== designer UI renders headless"
"$BIN" --ui-snapshot "$T/ui.png" "$T/demo.rsc" tree 1 obj 4 || fail "ui snapshot"
"$BIN" --ui-snapshot "$T/ui2.png" "$T/demo.rsc" tree 1 test key A || fail "ui test mode"
"$BIN" --ui-snapshot "$T/ui3.png" "$T/demo.rsc" tree 1 obj 21 bitmap || fail "ui bitmap editor"

echo "All tests passed."
