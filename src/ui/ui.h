// A small immediate-mode widget toolkit drawn with Canvas.
#pragma once
#include "platform/window.h"
#include "ui/canvas.h"
#include <functional>
#include <string>
#include <vector>

namespace ui {

struct Input {
    int mx = -10000, my = -10000;
    bool down[3] = {}, pressed[3] = {}, released[3] = {};
    bool dbl = false;               // left press was a double click
    double wheelX = 0, wheelY = 0;
    std::vector<platform::KeyEvent> keys;
    uint32_t mods = 0;
};

using Id = uint64_t;

class Ui {
public:
    Canvas& c;
    Input in;
    bool enabled = true;          // false: widgets draw but ignore input
    Id focus = 0;                 // keyboard focus (text fields)
    Id active = 0;                // widget being dragged
    int cursor = 0;               // text cursor in focused field
    Id popup = 0;                 // currently open popup
    Rect popupAnchor;

    explicit Ui(Canvas& canvas) : c(canvas) {}

    void beginFrame();
    void endFrame();              // draws overlays (popups)

    // ids
    static Id hash(const std::string& s, Id seed = 1469598103934665603ull);
    Id id(const std::string& s) const { return hash(s, idSeed()); }
    Id id(int n) const { return hash(std::to_string(n), idSeed() ^ 0x9E3779B97F4A7C15ull); }
    void pushId(const std::string& s) { ids_.push_back(id(s)); }
    void popId() { if (!ids_.empty()) ids_.pop_back(); }

    // input helpers
    bool hover(const Rect& r) const;
    bool clicked(const Rect& r, int button = 0) const { return hover(r) && in.pressed[button]; }
    bool dblClicked(const Rect& r) const { return hover(r) && in.pressed[0] && in.dbl; }
    bool wantsKeyboard() const { return focus != 0; }
    void consumeKeys() { in.keys.clear(); }
    void consumeClick() { for (auto& p : in.pressed) p = false; }

    // widgets
    bool button(const Rect& r, const std::string& label, bool enabledFlag = true, bool toggled = false);
    bool smallButton(const Rect& r, const std::string& label, bool enabledFlag = true);
    bool checkbox(const Rect& r, const std::string& label, bool& v, bool enabledFlag = true);
    bool radio(const Rect& r, const std::string& label, bool on);
    void label(const Rect& r, const std::string& s, uint32_t color = col::text, int align = 0);
    void header(const Rect& r, const std::string& s);
    // Returns true when the text changed (s is modified in place).
    bool textField(Id fid, const Rect& r, std::string& s, int maxLen = 255, bool enabledFlag = true);
    bool intField(Id fid, const Rect& r, int& v, int lo, int hi, bool enabledFlag = true);
    bool hexField(Id fid, const Rect& r, uint32_t& v, int digits, bool enabledFlag = true);
    // Dropdown: returns true when the user picked a new value.
    bool dropdown(Id did, const Rect& r, int& sel, const std::vector<std::string>& items, bool enabledFlag = true,
                  const std::function<void(int, const Rect&)>& drawItem = nullptr);
    // Popup menu at a position; returns chosen item index (on a later frame) or -1.
    void openMenu(Id mid, int x, int y, const std::vector<std::string>& items, const std::vector<bool>& enabledItems = {});
    int menuResult(Id mid);
    bool isPopupOpen() const { return popup != 0; }
    // A popup choice is waiting to be returned by dropdown()/menuResult().
    bool hasPendingResult() const { return resultId_ != 0; }

    // scrollbar for a list; returns true while dragging
    void scrollbar(Id sid, const Rect& r, int content, int view, int& offset);
    void focusField(Id fid, int cursorPos = -1) { focus = fid; cursor = cursorPos; }
    void dropFocus() { focus = 0; }

private:
    std::vector<Id> ids_;
    Id idSeed() const { return ids_.empty() ? 0 : ids_.back(); }
    struct Overlay { Rect r; std::function<void()> fn; };
    std::vector<Overlay> overlays_;
    std::vector<Rect> block_, nextBlock_;
    Id resultId_ = 0;
    int resultVal_ = -1;
    bool inOverlay_ = false;
    int dragOff_ = 0;
    bool focusSeen_ = false;
    bool popupDrawn_ = false;
    std::vector<std::string> menuItems_;
    std::vector<bool> menuEnabled_;

    bool blocked() const;
    void popupList(Id pid, const Rect& r, const std::vector<std::string>& items, const std::vector<bool>& en,
                   int sel, const std::function<void(int, const Rect&)>& drawItem);
};

} // namespace ui
