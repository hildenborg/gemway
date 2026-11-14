// The GEMWay designer application.
#pragma once
#include "gem/scene.h"
#include "platform/window.h"
#include "rsc/rsc.h"
#include "ui/ui.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

using gem::ItemKind;
using ui::Rect;

class App;

struct Modal {
    virtual ~Modal() = default;
    // Draw and handle input; return false to close the dialog.
    virtual bool draw(App& app, ui::Ui& u) = 0;
};

struct Settings {
    int aes = 1, res = 2, zoom = 2;
    bool grid = false, snap = true, outlines = true;
    bool writeHrd = true, writeDef = false, writeH = true;
    int headerLang = 0;            // HeaderLang
    bool writeRsh = false;
    std::string lastDir;
    std::vector<std::string> recent;
    void load();
    void save() const;
};

class App : public platform::Handler {
public:
    App();
    int run(const std::string& file, int aes, int res);

    // ---- platform::Handler
    void onPointer(double x, double y) override;
    void onButton(int button, bool down) override;
    void onScroll(double dx, double dy) override;
    void onKey(const platform::KeyEvent& e) override;
    void onPointerLeave() override;
    bool onClose() override;
    void render(uint32_t* px, int w, int h, int stride, int scale) override;
    bool wantsRedraw() override { return redraw_; }
    platform::Cursor cursor() override { return cursor_; }

    // ---- document
    rsc::Rsc doc;
    std::string path;              // current file (empty: untitled)
    bool dirty = false;
    void checkpoint(ui::Id editSession = 0);   // push undo state before a change
    void touch() { dirty = true; updateTitle(); }
    void undo();
    void redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // ---- selection
    ItemKind selKind = ItemKind::Tree;
    int selItem = -1;
    std::vector<int> selObjs;      // selected objects in the current tree (last = primary)
    rsc::Tree* curTree();
    int primaryObj() const { return selObjs.empty() ? -1 : selObjs.back(); }
    void selectItem(ItemKind k, int idx);
    void selectObj(int obj, bool toggle = false);
    bool isSelected(int obj) const;
    void clampSelection();

    // ---- view
    Settings set;
    bool testMode = false;
    int openMenuTitle = -1;        // menu title index whose dropdown is shown
    int alertDefault = 1;
    gem::Env env;
    gem::Screen screen;
    gem::Scene scene;
    void setMode(int aes, int res);
    int cellW() const { return env.res.cellW; }
    int cellH() const { return env.res.cellH; }

    // ---- file operations
    void newDoc();
    bool openDoc(const std::string& file);
    bool saveDoc(const std::string& file);
    void cmdOpen();
    void cmdSave();
    void cmdSaveAs();
    void cmdExport(int which);
    void cmdImportNames();
    void confirmDiscard(std::function<void()> then);
    void addRecent(const std::string& f);
    void quit();

    // ---- editing commands
    void addItem(int what);        // 0 dialog 1 menu 2 alert 3 string 4 image
    void deleteItem();
    void duplicateItem();
    void moveItem(int dir);
    void addObject(int type);
    void deleteSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void duplicateSelection();
    void reorderSelection(int how);   // -1 back, +1 forward, -2 to back, +2 to front
    void moveIntoPrevSibling();
    void moveOutToParent();
    void nudge(int dx, int dy, bool resize, bool pixel);
    void alignSelection(int how);
    void sortCurrentTree();
    void autoName();
    void menuCommand(int cmd);     // menu tree helpers
    void editBitmap();             // open the bitmap / icon editor for the selection
    void fitParent();

    // ---- modals & messages
    std::unique_ptr<Modal> modal;
    std::vector<std::unique_ptr<Modal>> modalStack;
    void pushModal(Modal* m);
    void message(const std::string& title, const std::string& text);
    void ask(const std::string& title, const std::string& text, const std::vector<std::string>& buttons,
             std::function<void(int)> cb);
    void fileDialog(bool save, const std::string& title, const std::string& defName,
                    const std::vector<std::string>& exts, std::function<void(const std::string&)> cb);
    std::string status;
    void setStatus(const std::string& s) { status = s; }

    // ---- ui parts (app_ui.cpp, inspector.cpp, editor.cpp)
    platform::Window win;
    ui::Canvas canvas;
    ui::Ui u{ canvas };
    ui::Input pending;
    bool redraw_ = true;
    platform::Cursor cursor_ = platform::Cursor::Default;

    void frame();
    void drawMenuBar(const Rect& r);
    void drawToolbar(const Rect& r);
    void drawResourcePanel(const Rect& r);
    void drawOutlinePanel(const Rect& r);
    void drawPalette(const Rect& r);
    int drawInspector(const Rect& r, int scroll);
    void drawEditor(const Rect& r);
    void drawStatus(const Rect& r);
    void handleShortcuts();
    void updateTitle();

    // editor state (editor.cpp)
    struct DragState {
        enum Kind { None, Move, Resize, Pan } kind = None;
        int handle = 0;
        int startAx = 0, startAy = 0;
        int startMx = 0, startMy = 0;
        int startScrollX = 0, startScrollY = 0;
        bool moved = false;
        std::vector<int> objs;
        std::vector<Rect> orig;      // pixel rects (relative to parent)
    } drag;
    int scrollX = 0, scrollY = 0;
    bool fitPending = true;        // choose the largest zoom that shows the whole screen
    Rect viewRect;
    int viewOx = 0, viewOy = 0, viewZx = 1, viewZy = 1;
    int hoverObj = -1;
    // test mode
    rsc::Tree testTree;
    int testEdit = -1, testCursor = 0;
    void enterTestMode(bool on);
    void testClick(int obj);
    void testKey(const platform::KeyEvent& e);

    int inspectorScroll = 0;
    // stable widget id for a field of the current selection
    ui::Id fieldId(const char* name) const;
    int resScroll = 0, outlineScroll = 0;
    rsc::Tree clipboard;

private:
    std::vector<rsc::Rsc> undo_, redo_;
    ui::Id lastEdit_ = 0;
    int menuOpenIdx_ = -1;
    bool quitAsked_ = false;
};

// helpers shared by the UI files
std::string itemLabel(const rsc::Rsc& r, ItemKind k, int idx);
std::string objectLabel(const rsc::Tree& t, int idx);
const char* colourName(int c);
std::vector<std::string> colourNames(int n);
void drawSwatch(ui::Canvas& c, const Rect& r, uint32_t rgb);

Modal* makeBitmapEditor(App& app);
Modal* makeAboutDialog();
Modal* makeShortcutsDialog();
Modal* makeSettingsDialog(App& app);

} // namespace app
