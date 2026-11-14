// Native Wayland window: libdecor (decorations, xdg-shell), wl_shm
// software rendering, pointer with cursor theme and xkbcommon keyboard.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace platform {

enum Mod : uint32_t { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4, MOD_SUPER = 8 };

// keysyms we use (values from xkbcommon-keysyms.h)
enum Key : uint32_t {
    K_BACKSPACE = 0xff08, K_TAB = 0xff09, K_RETURN = 0xff0d, K_ESCAPE = 0xff1b, K_DELETE = 0xffff,
    K_HOME = 0xff50, K_LEFT = 0xff51, K_UP = 0xff52, K_RIGHT = 0xff53, K_DOWN = 0xff54,
    K_PAGEUP = 0xff55, K_PAGEDOWN = 0xff56, K_END = 0xff57, K_INSERT = 0xff63, K_KP_ENTER = 0xff8d,
    K_F1 = 0xffbe, K_F2 = 0xffbf, K_F3 = 0xffc0, K_F5 = 0xffc2, K_ISO_LEFT_TAB = 0xfe20,
};

struct KeyEvent {
    uint32_t sym = 0;        // lower-case keysym for letters
    uint32_t mods = 0;
    std::string text;        // UTF-8 produced by the key (empty for control keys)
    bool repeat = false;
};

enum class Cursor { Default, Text, Move, ResizeNWSE, ResizeNESW, ResizeNS, ResizeEW, Crosshair, Pointer };

class Handler {
public:
    virtual ~Handler() = default;
    virtual void onPointer(double x, double y) = 0;           // logical coordinates
    virtual void onButton(int button, bool down) = 0;         // 0 left, 1 right, 2 middle
    virtual void onScroll(double dx, double dy) = 0;          // in "notches"
    virtual void onKey(const KeyEvent& e) = 0;
    virtual void onPointerLeave() {}
    virtual bool onClose() = 0;                               // return true to quit
    // Render into an ARGB8888 buffer of w x h physical pixels (scale = buffer scale).
    virtual void render(uint32_t* px, int w, int h, int stride, int scale) = 0;
    virtual bool wantsRedraw() = 0;
    virtual Cursor cursor() { return Cursor::Default; }
    virtual void onIdle() {}
};

class Window {
public:
    Window();
    ~Window();
    bool open(const std::string& title, int w, int h, Handler* handler);
    void setTitle(const std::string& title);
    int run();             // event loop, returns exit code
    void quit();
    int logicalWidth() const;
    int logicalHeight() const;
    std::string error() const;

    struct Impl;
private:
    std::unique_ptr<Impl> d;
};

} // namespace platform
