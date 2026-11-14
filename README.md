# GEMWay – GEM resource designer for Atari computers

GEMWay is a native Wayland application for designing GEM user interfaces
(dialogs, menus, alerts, icons and images) for the Atari ST, STE, TT and
Falcon. It reads and writes standard `.RSC` resource files and the
companion header and name files. It renders every object pixel for pixel
the way the Atari AES draws it.

## Building

Requirements: a C++20 compiler, `make`, `pkg-config` and the development
packages for `wayland-client`, `wayland-cursor`, `xkbcommon` and `libdecor-0`.

```sh
sudo apt install g++ make pkg-config libwayland-dev libxkbcommon-dev libdecor-0-dev   # Debian/Ubuntu
make            # builds ./gemway
make test       # self tests, no display needed
sudo make install
```

Run `./gemway [file.rsc]`. Set `GEMWAY_SCALE=2` to force a UI scale on
HiDPI screens; otherwise the output scale is detected automatically.

## Features

**Target systems.** You can switch the AES version and screen mode at any time:

| AES | Systems |
| --- | --- |
| 1.20 | TOS 1.00/1.02 |
| 1.40 | TOS 1.04/1.06/1.62 |
| 3.00–3.20 | TOS 2.0x/3.0x |
| 3.40 | TOS 4.0x Falcon: 3D objects, colour icons, submenu flag |
| 4.10 | MultiTOS |

Screen modes: ST Low/Medium/High, TT Low/Medium/High, and Falcon
16/256 colours and mono. Each mode uses its real character cell (8x8 or
8x16), bit depth, hardware palette and pixel aspect.

**Faithful rendering.** The VDI layer keeps hardware pixel values in bit
planes, so the writing modes (replace, transparent, XOR, erase), the dither
patterns aligned to the screen, and colour mapping all behave like the
real machine. The AES layer re-implements `objc_draw`:

* border thickness, including the extra outside line of negative borders
* the EXIT/DEFAULT button borders
* OUTLINED, SHADOWED, CHECKED, CROSSED, DISABLED, SELECTED and WHITEBAK
* text justification and clipping, and `ob_format` templates
* icons and colour icons, including the TOS plane expansion and the dithered "selected" look
* AES 3.40 3D indicators, activators and backgrounds

`form_alert` layout, `form_center`, the menu bar and dropdowns all follow
the AES logic.

**Object types.** The designer edits every type: BOX, IBOX, BOXCHAR,
STRING, BUTTON, TITLE, TEXT, BOXTEXT, FTEXT, FBOXTEXT, IMAGE, ICON, CICON
and USERDEF. It also keeps the MagiC/AES 4 extended types and extended
object types (the high byte of `ob_type`).

**Editing:**

* Select and multi-select; move and resize with handles, snapping to the character grid (hold Alt for pixel precision).
* Arrow-key nudging; cut, copy, paste and duplicate; unlimited undo/redo.
* Draw order (forward/backward), re-parenting, alignment, fit-to-children and renumbering.
* Automatic naming of interactive objects.
* An inspector for every field of OBJECT, TEDINFO, ICONBLK and BITBLK, with colour pickers showing the current palette.

**Menus.** Menus are created with the standard RCS layout: the Desk menu with 6 accessory slots, titles and dropdowns. You can add titles, items and separators, and the layout is recalculated automatically.

**Alerts.** An alert editor (icon, up to 5 lines, up to 3 buttons, TOS length warnings) with a live `form_alert` preview.

**Image and icon editor.** Pencil, line, rectangle, fill and colour-pick tools, plus masks, flip, shift, invert and resize. It also edits colour icon resolutions (4 and 8 planes) and their optional "selected" images, with a live preview.

**Test mode (F5).** This simulates `form_do`: selectable and radio buttons,
EXIT/TOUCHEXIT/DEFAULT, editable fields with template and validation
characters, and menu tracking.

## File formats

| Read | Write |
| --- | --- |
| `.RSC` (classic and TOS 4 format with CICONBLKs) | `.RSC` (TOS 4 format only when colour icons are used) |
| `.HRD`, `.DEF`/`.RSD`, `.DFN`, `.H` name files | `.HRD`, `.DEF` name files |
| | `.H` (C), `.I` (Pascal), `.LST` (GFA), `.S` (asm), `.M2` (Modula-2) |
| | `.RSH` – classic RCS C source with the resource embedded |

When you open a resource, names are loaded automatically from a sibling
`.HRD`, `.DEF`, `.RSD`, `.DFN` or `.H` file. **File → Save options**
controls which companion files are written when you save.

## Command line

```
gemway --dump file.rsc                      structure listing
gemway --render file.rsc ITEM out.png       ITEM: tree index or name, s<n>, i<n>
       [--aes N] [--res N] [--zoom N] [--menu N]
gemway --alert "[1][Text][OK]" out.png
gemway --convert in.rsc out.rsc             re-save (and write .HRD)
gemway --header file.rsc [c|pascal|gfa|asm|modula|rsh]
gemway --make-example out.rsc               demo resource using every feature
gemway --modes                              list AES versions and screen modes
```

`data/examples/DEMO.RSC` (with `.HRD`, `.H` and `.RSH`) shows every object
type, a menu, 3D objects, alerts, a colour icon and a free image.

## Accuracy notes

* The system fonts (6x6, 8x8, 8x16), the default TT/Falcon palettes and the
  three alert icons come from EmuTOS. The fonts reproduce the Atari ones, but
  EmuTOS's alert icons are its own designs. To use the original TOS icons,
  load an RSC whose first three free images are the 32x32 NOTE, QUESTION and
  STOP icons (**File → Save options**). `tools/emutos_fonts.py` regenerates
  `src/gem/sysfonts.cpp` from EmuTOS font sources.
* 3D effects are drawn for AES 3.40+ in modes with 16 or more colours.
* GDOS fonts (`te_font` 0–2) are shown in the system font. USERDEF objects,
  which a program draws at run time, are shown as a grey placeholder.

## Source layout

```
src/base      byte order helpers, Atari character set, PNG writer
src/rsc       resource model, RSC reader/writer, name files, exports
src/gem       VDI and AES emulation (fonts, palettes, objc_draw, alerts, menus)
src/platform  Wayland window (libdecor, wl_shm, xkbcommon, cursors)
src/ui        software canvas and immediate-mode widgets
src/app       the designer: panels, inspector, editor, dialogs, bitmap editor
tests         self test script (make test)
```

## Licence

The font data, palettes and alert icons are taken from EmuTOS and are
licensed under GPL-2.0-or-later (© The EmuTOS development team). A binary
built from this tree therefore falls under the GPL.
