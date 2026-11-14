# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

GEMWay is a C++20 Wayland application for designing Atari GEM resources (`.RSC`). It renders objects pixel for pixel the way the Atari AES draws them, for TOS 1.x–4.x and MultiTOS on ST, TT and Falcon screen modes.

## Commands

```sh
make -j8                 # build ./gemway (objects in build/, auto header deps)
make test                # tests/run_tests.sh ./gemway  (headless, no display needed)
make clean
```

There is no test framework. `tests/run_tests.sh` is a shell script made of independent sections. To run one check, copy the relevant line(s) out of it. Most useful single checks:

```sh
./gemway --make-example /tmp/d.rsc && ./gemway --convert /tmp/d.rsc /tmp/r.rsc && cmp /tmp/d.rsc /tmp/r.rsc   # writer round trip must be byte identical
./gemway --render FILE.rsc ITEM out.png --aes N --res N [--menu N] [--zoom N]   # ITEM: tree idx/name, s<n>, i<n>
./gemway --alert "[1][Text][OK]" out.png
./gemway --dump FILE.rsc
./gemway --modes                                     # list --aes / --res indices
```

### Verifying GUI changes without a display

`--ui-snapshot` renders the whole designer UI offscreen (1400x900) to a PNG. Steps are applied in order:

```sh
./gemway --ui-snapshot out.png FILE.rsc tree 1 obj 4        # select tree/object
./gemway --ui-snapshot out.png FILE.rsc tree 1 test key A   # test mode + key press
./gemway --ui-snapshot out.png FILE.rsc tree 1 obj 21 bitmap   # open the bitmap editor
```

Other steps: `string N`, `image N`, `menu N`, `aes N`, `res N`, `zoom N`, `about`, `save`, `click X Y`, `move X Y`. They are implemented in `snapshotGui()` at the end of `src/app/app.cpp`. Read the resulting PNG to inspect it. The real Wayland window can only be smoke-tested, for example `timeout 3 ./gemway` or `WAYLAND_DEBUG=1`, because screenshots are blocked on GNOME.

## Architecture

Layers (each depends only on those above it):

1. **`src/base`**: big-endian read/write helpers (`Writer`), file and path helpers, the Atari ST charset ↔ UTF-8 conversion, and a minimal PNG writer.
2. **`src/rsc`**: the resource model, I/O and exports. This layer has no rendering code.
3. **`src/gem`**: VDI + AES emulation. Turns an `rsc::Tree` into pixels for a given `Env`.
4. **`src/platform`**: Wayland window (libdecor for decorations/xdg-shell, wl_shm double buffering, xkbcommon keyboard with key repeat, wayland-cursor). It is fully decoupled through the `platform::Handler` interface.
5. **`src/ui`**: software `Canvas` (logical px × integer output scale, text in the Atari 8x16 font) and an immediate-mode widget toolkit (`ui::Ui`).
6. **`src/app`**: the designer (`app::App` implements `platform::Handler`).

### Resource model (`rsc/rsc.h`): important invariants

- Trees are **flat GEM object arrays linked by `next/head/tail`**, exactly like on the Atari. Object indices are part of the user's program API (header `#define`s), so they must never be renumbered implicitly. Edit trees only through the link-aware helpers in `rsc.cpp` (`addObject`, `deleteObject`, `reorderObject`, `reparentObject`, `copySubtree`, `sortTree`). These keep `LASTOB` correct. Only `sortTree` renumbers, and only on explicit user request (and for freshly created menus).
- Coordinates are stored **raw**, as in the file: low byte = characters, high byte = pixel offset (signed, ">128 is negative"). Convert with `toPixels(raw, cell, isWidth, screenW)` / `fromPixels`. A width of 80 characters means "screen width". Pixel size therefore depends on the selected resolution's cell size (8x8 or 8x16).
- Each `Object` carries every possible spec payload (`spec`, `str`, `ted`, `icon`, `cicons`, `bit`), and `baseType()` selects which one is meaningful. The high byte of `type` is the extended type and must be preserved.
- Text is stored as raw **Atari charset bytes**. Convert only at boundaries (UI input, file names, `--dump`).
- `rsc_io.cpp`: the writer emits a deterministic layout. The extension array and CICONBLK data are written only when colour icons exist (`rsh_vrsn` bit 2). `te_txtlen` from the file is preserved. Load→save must stay byte-identical (tested).
- Name files: `.HRD` / `.DEF` / `.DFN` binary formats in `names.cpp` (formats verified against EmuTOS's `erd` tool and real files). `.H` parsing is heuristic and comment-driven. On load, names are taken from the first sibling file found, in the order HRD, DEF, RSD, DFN, H.

### Rendering (`src/gem`): where pixel fidelity lives

- `vdi.cpp` `Screen` stores **hardware pixel values** (bit-plane values), not VDI colour indices. `pen()` maps VDI colour → pen per plane count, and invalid colours map to BLACK. This is what makes XOR, the write modes and the screen-aligned dither patterns behave like the real hardware.
- `aes.cpp` re-implements the AES drawing code as reproduced by EmuTOS (`aes/gemoblib.c`, `gemgraf.c`, `gemfmalt.c`): `justDraw` (objc_draw), `grBox` (the outward border has an extra line, `th--`), `drawIcon` (gr_gicon incl. colour icon plane expansion), `buildAlert` (form_alert/fm_build), `center` (form_center), and the menu bar (`hbox = cellH + 3` with a line at `hbox-1`). When changing drawing behaviour, match the EmuTOS/TOS source semantics, including C integer division and rounding, rather than "improving" the look.
- `Env` (`env.cpp`) combines an `AesProfile` (version-gated features: colour icons ≥ 0x0330, 3D ≥ 0x0340) with a `Resolution`. `use3D()` additionally requires ≥16 colours. Objects unknown to the selected AES version (e.g. G_CICON before 3.30) are deliberately not drawn.
- `scene.cpp` composes a full screen for one resource item: desktop pattern, centred dialog, menu bar plus the open dropdown, or a generated alert. It is used by both the CLI (`--render`) and the GUI editor.
- Generated data files: `sysfonts.cpp` (from `tools/emutos_fonts.py`), `palettes.cpp` and `sysicons.cpp` (EmuTOS, GPL-2.0-or-later). Do not hand-edit them.

### Designer (`src/app`)

- **Frame model:** `App::render()` snapshots the accumulated input into `ui.in` and rebuilds the whole UI in `frame()` (app_ui.cpp). Any frame that had input schedules another frame, so state changes made mid-frame become visible. The order inside `frame()` matters: the editor and inspector run before `handleShortcuts()`, so focused text fields consume keys first. While a field has focus, only Ctrl shortcuts reach the app.
- **Popups and menus:** they are deferred overlays. Their results arrive on the *next* frame through `Ui::dropdown`/`Ui::menuResult`. A popup that isn't re-requested every frame closes automatically.
- **Modals:** `App::pushModal`. They are drawn after the base UI, which runs with `ui.enabled = false`, and they return `false` from `draw()` to close.
- **Undo:** whole-`Rsc` snapshots. Call `checkpoint()` before mutating `doc`. Continuous edits pass a session id (e.g. `fieldId("text")`, the nudge id), so repeated changes merge into one undo step. Inspector widgets use `fieldId(name)`, which is stable per selection (kind/item/object) so focus survives frames.
- **Test mode:** works on a copy, `testTree`, and never modifies `doc`.
- Settings persist in `$XDG_CONFIG_HOME/gemway/settings.conf`.
