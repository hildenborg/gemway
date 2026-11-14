#include "platform/window.h"

#include <libdecor.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace platform {

using Clock = std::chrono::steady_clock;

struct ShmBuffer {
    wl_buffer* buf = nullptr;
    uint32_t* data = nullptr;
    size_t size = 0;
    int w = 0, h = 0;
    bool busy = false;
};

struct Window::Impl {
    Handler* handler = nullptr;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    struct Output { wl_output* out; int scale; };
    std::vector<Output> outputs;
    std::vector<wl_output*> surfaceOutputs;

    wl_surface* surface = nullptr;
    libdecor* ctx = nullptr;
    libdecor_frame* frame = nullptr;
    int w = 1280, h = 800, scale = 1;
    bool configured = false, running = true, framePending = false, dirty = true;
    ShmBuffer bufs[2];

    wl_cursor_theme* cursorTheme = nullptr;
    int cursorThemeScale = 0;
    wl_surface* cursorSurface = nullptr;
    uint32_t pointerSerial = 0;
    bool pointerIn = false;
    Cursor curCursor = Cursor::Default;
    bool cursorSet = false;

    xkb_context* xkb = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* xkbState = nullptr;
    uint32_t mods = 0;
    int repeatRate = 25, repeatDelay = 500;
    uint32_t repeatKey = 0;
    KeyEvent repeatEvent;
    Clock::time_point repeatNext;
    bool repeating = false;

    std::string err;

    void draw();
    bool makeBuffer(ShmBuffer& b, int pw, int ph);
    void freeBuffer(ShmBuffer& b);
    void updateCursor(bool force = false);
    void updateScale();
};

// ------------------------------------------------------------------ buffers
static void bufferRelease(void* data, wl_buffer*)
{
    static_cast<ShmBuffer*>(data)->busy = false;
}
static const wl_buffer_listener kBufferListener = { bufferRelease };

void Window::Impl::freeBuffer(ShmBuffer& b)
{
    if (b.buf) wl_buffer_destroy(b.buf);
    if (b.data) munmap(b.data, b.size);
    b = ShmBuffer();
}

bool Window::Impl::makeBuffer(ShmBuffer& b, int pw, int ph)
{
    freeBuffer(b);
    int stride = pw * 4;
    size_t size = size_t(stride) * ph;
    int fd = memfd_create("gemway-shm", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, off_t(size)) < 0) { close(fd); return false; }
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { close(fd); return false; }
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, int32_t(size));
    b.buf = wl_shm_pool_create_buffer(pool, 0, pw, ph, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    b.data = static_cast<uint32_t*>(p);
    b.size = size;
    b.w = pw;
    b.h = ph;
    b.busy = false;
    wl_buffer_add_listener(b.buf, &kBufferListener, &b);
    return true;
}

static void frameDone(void* data, wl_callback* cb, uint32_t)
{
    wl_callback_destroy(cb);
    static_cast<Window::Impl*>(data)->framePending = false;
}
static const wl_callback_listener kFrameListener = { frameDone };

void Window::Impl::draw()
{
    if (!configured) return;
    int pw = w * scale, ph = h * scale;
    ShmBuffer* b = nullptr;
    for (auto& cand : bufs)
        if (!cand.busy) { b = &cand; break; }
    if (!b) return;   // both buffers held by the compositor; retry later
    if (b->w != pw || b->h != ph || !b->buf)
        if (!makeBuffer(*b, pw, ph)) { err = "cannot allocate shared memory buffer"; return; }
    handler->render(b->data, pw, ph, pw, scale);
    wl_surface_set_buffer_scale(surface, scale);
    wl_surface_attach(surface, b->buf, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, pw, ph);
    wl_callback* cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &kFrameListener, this);
    framePending = true;
    b->busy = true;
    wl_surface_commit(surface);
    dirty = false;
}

// ------------------------------------------------------------------ cursor
void Window::Impl::updateCursor(bool force)
{
    if (!pointer || !pointerIn) return;
    Cursor c = handler->cursor();
    if (!force && cursorSet && c == curCursor && cursorThemeScale == scale) return;
    if (!cursorTheme || cursorThemeScale != scale) {
        if (cursorTheme) wl_cursor_theme_destroy(cursorTheme);
        const char* sz = getenv("XCURSOR_SIZE");
        int size = sz ? atoi(sz) : 24;
        if (size <= 0) size = 24;
        cursorTheme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), size * scale, shm);
        cursorThemeScale = scale;
    }
    if (!cursorTheme) return;
    static const char* names[][3] = {
        { "left_ptr", "default", "arrow" },
        { "xterm", "text", "ibeam" },
        { "fleur", "move", "all-scroll" },
        { "bottom_right_corner", "nwse-resize", "size_fdiag" },
        { "bottom_left_corner", "nesw-resize", "size_bdiag" },
        { "sb_v_double_arrow", "ns-resize", "v_double_arrow" },
        { "sb_h_double_arrow", "ew-resize", "h_double_arrow" },
        { "crosshair", "cross", "tcross" },
        { "hand2", "pointer", "hand1" },
    };
    wl_cursor* cur = nullptr;
    for (const char* n : names[int(c)])
        if ((cur = wl_cursor_theme_get_cursor(cursorTheme, n))) break;
    if (!cur) cur = wl_cursor_theme_get_cursor(cursorTheme, "left_ptr");
    if (!cur || cur->image_count == 0) return;
    wl_cursor_image* img = cur->images[0];
    wl_buffer* buf = wl_cursor_image_get_buffer(img);
    if (!cursorSurface) cursorSurface = wl_compositor_create_surface(compositor);
    wl_surface_set_buffer_scale(cursorSurface, scale);
    wl_surface_attach(cursorSurface, buf, 0, 0);
    wl_surface_damage_buffer(cursorSurface, 0, 0, int(img->width), int(img->height));
    wl_surface_commit(cursorSurface);
    wl_pointer_set_cursor(pointer, pointerSerial, cursorSurface, int(img->hotspot_x) / scale,
                          int(img->hotspot_y) / scale);
    curCursor = c;
    cursorSet = true;
}

// ------------------------------------------------------------------ pointer
static void pEnter(void* data, wl_pointer*, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (s != d->surface) return;
    d->pointerIn = true;
    d->pointerSerial = serial;
    d->updateCursor(true);
    d->handler->onPointer(wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void pLeave(void* data, wl_pointer*, uint32_t, wl_surface* s)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (s != d->surface) return;
    d->pointerIn = false;
    d->cursorSet = false;
    d->handler->onPointerLeave();
}
static void pMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (!d->pointerIn) return;
    d->handler->onPointer(wl_fixed_to_double(x), wl_fixed_to_double(y));
    d->updateCursor();
}
static void pButton(void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (!d->pointerIn) return;
    d->pointerSerial = serial;
    int b = button == BTN_LEFT ? 0 : button == BTN_RIGHT ? 1 : button == BTN_MIDDLE ? 2 : -1;
    if (b >= 0) d->handler->onButton(b, state == WL_POINTER_BUTTON_STATE_PRESSED);
    d->updateCursor();
}
static void pAxis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (!d->pointerIn) return;
    double v = wl_fixed_to_double(value) / 10.0;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) d->handler->onScroll(0, v);
    else d->handler->onScroll(v, 0);
}
static void pFrame(void*, wl_pointer*) {}
static void pAxisSource(void*, wl_pointer*, uint32_t) {}
static void pAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
static void pAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

// ------------------------------------------------------------------ keyboard
static void kKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map == MAP_FAILED) { close(fd); return; }
    xkb_keymap* km = xkb_keymap_new_from_string(d->xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);
    if (!km) return;
    if (d->xkbState) xkb_state_unref(d->xkbState);
    if (d->keymap) xkb_keymap_unref(d->keymap);
    d->keymap = km;
    d->xkbState = xkb_state_new(km);
}
static void kEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
static void kLeave(void* data, wl_keyboard*, uint32_t, wl_surface*)
{
    static_cast<Window::Impl*>(data)->repeating = false;
}
static void kKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (!d->xkbState) return;
    uint32_t code = key + 8;
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (d->repeating && d->repeatKey == code) d->repeating = false;
        return;
    }
    KeyEvent e;
    e.sym = xkb_state_key_get_one_sym(d->xkbState, code);
    if (e.sym >= 'A' && e.sym <= 'Z') e.sym += 'a' - 'A';
    e.mods = d->mods;
    char buf[64];
    int n = xkb_state_key_get_utf8(d->xkbState, code, buf, sizeof buf);
    if (n > 0 && !(d->mods & (MOD_CTRL | MOD_ALT | MOD_SUPER)) && (unsigned char)buf[0] >= 32 && buf[0] != 127)
        e.text.assign(buf, size_t(n));
    d->handler->onKey(e);
    if (d->keymap && xkb_keymap_key_repeats(d->keymap, code) && d->repeatRate > 0) {
        d->repeating = true;
        d->repeatKey = code;
        d->repeatEvent = e;
        d->repeatEvent.repeat = true;
        d->repeatNext = Clock::now() + std::chrono::milliseconds(d->repeatDelay);
    }
}
static void kModifiers(void* data, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (!d->xkbState) return;
    xkb_state_update_mask(d->xkbState, dep, lat, lock, 0, 0, group);
    uint32_t m = 0;
    auto on = [&](const char* name) {
        return xkb_state_mod_name_is_active(d->xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    if (on(XKB_MOD_NAME_SHIFT)) m |= MOD_SHIFT;
    if (on(XKB_MOD_NAME_CTRL)) m |= MOD_CTRL;
    if (on(XKB_MOD_NAME_ALT)) m |= MOD_ALT;
    if (on(XKB_MOD_NAME_LOGO)) m |= MOD_SUPER;
    d->mods = m;
}
static void kRepeatInfo(void* data, wl_keyboard*, int32_t rate, int32_t delay)
{
    auto* d = static_cast<Window::Impl*>(data);
    d->repeatRate = rate;
    d->repeatDelay = delay;
}

// ------------------------------------------------------------------ seat / output / registry
static void seatCaps(void* data, wl_seat* seat, uint32_t caps)
{
    auto* d = static_cast<Window::Impl*>(data);
    static wl_pointer_listener pl;
    static wl_keyboard_listener kl;
    static bool init = false;
    if (!init) {
        memset(&pl, 0, sizeof pl);
        pl.enter = pEnter; pl.leave = pLeave; pl.motion = pMotion; pl.button = pButton; pl.axis = pAxis;
        pl.frame = pFrame; pl.axis_source = pAxisSource; pl.axis_stop = pAxisStop; pl.axis_discrete = pAxisDiscrete;
        memset(&kl, 0, sizeof kl);
        kl.keymap = kKeymap; kl.enter = kEnter; kl.leave = kLeave; kl.key = kKey; kl.modifiers = kModifiers;
        kl.repeat_info = kRepeatInfo;
        init = true;
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !d->pointer) {
        d->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(d->pointer, &pl, d);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
        d->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(d->keyboard, &kl, d);
    }
}
static void seatName(void*, wl_seat*, const char*) {}

static void outGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
static void outMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
static void outDone(void*, wl_output*) {}
static void outScale(void* data, wl_output* o, int32_t factor)
{
    auto* d = static_cast<Window::Impl*>(data);
    for (auto& out : d->outputs)
        if (out.out == o) out.scale = factor;
    d->updateScale();
}

void Window::Impl::updateScale()
{
    int s = 1;
    for (wl_output* o : surfaceOutputs)
        for (auto& out : outputs)
            if (out.out == o) s = std::max(s, out.scale);
    if (surfaceOutputs.empty())
        for (auto& out : outputs) s = std::max(s, out.scale);
    if (const char* env = getenv("GEMWAY_SCALE")) s = std::clamp(atoi(env), 1, 4);
    if (s != scale) {
        scale = s;
        dirty = true;
        updateCursor(true);
    }
}

static void surfEnter(void* data, wl_surface*, wl_output* o)
{
    auto* d = static_cast<Window::Impl*>(data);
    d->surfaceOutputs.push_back(o);
    d->updateScale();
}
static void surfLeave(void* data, wl_surface*, wl_output* o)
{
    auto* d = static_cast<Window::Impl*>(data);
    d->surfaceOutputs.erase(std::remove(d->surfaceOutputs.begin(), d->surfaceOutputs.end(), o), d->surfaceOutputs.end());
    d->updateScale();
}

static void regGlobal(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t version)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (!strcmp(iface, wl_compositor_interface.name)) {
        d->compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        d->shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (!strcmp(iface, wl_seat_interface.name) && !d->seat) {
        d->seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        static wl_seat_listener sl;
        memset(&sl, 0, sizeof sl);
        sl.capabilities = seatCaps;
        sl.name = seatName;
        wl_seat_add_listener(d->seat, &sl, d);
    } else if (!strcmp(iface, wl_output_interface.name) && version >= 2) {
        auto* o = static_cast<wl_output*>(wl_registry_bind(reg, name, &wl_output_interface, 2));
        static wl_output_listener ol;
        memset(&ol, 0, sizeof ol);
        ol.geometry = outGeometry; ol.mode = outMode; ol.done = outDone; ol.scale = outScale;
        wl_output_add_listener(o, &ol, d);
        d->outputs.push_back({ o, 1 });
    }
}
static void regRemove(void*, wl_registry*, uint32_t) {}
static const wl_registry_listener kRegistry = { regGlobal, regRemove };

// ------------------------------------------------------------------ libdecor
static void decorError(libdecor*, libdecor_error, const char* msg)
{
    fprintf(stderr, "libdecor error: %s\n", msg);
}

static void frameConfigure(libdecor_frame* frame, libdecor_configuration* cfg, void* data)
{
    auto* d = static_cast<Window::Impl*>(data);
    int w = 0, h = 0;
    if (!libdecor_configuration_get_content_size(cfg, frame, &w, &h) || w <= 0 || h <= 0) {
        w = d->w;
        h = d->h;
    }
    d->w = w;
    d->h = h;
    libdecor_state* st = libdecor_state_new(w, h);
    libdecor_frame_commit(frame, st, cfg);
    libdecor_state_free(st);
    d->configured = true;
    d->framePending = false;
    d->dirty = true;
    d->draw();
}
static void frameClose(libdecor_frame*, void* data)
{
    auto* d = static_cast<Window::Impl*>(data);
    if (d->handler->onClose()) d->running = false;
}
static void frameCommit(libdecor_frame*, void* data)
{
    wl_surface_commit(static_cast<Window::Impl*>(data)->surface);
}
static void frameDismissPopup(libdecor_frame*, const char*, void*) {}

// ------------------------------------------------------------------ Window
Window::Window() : d(new Impl) {}

Window::~Window()
{
    for (auto& b : d->bufs) d->freeBuffer(b);
    if (d->frame) libdecor_frame_unref(d->frame);
    if (d->ctx) libdecor_unref(d->ctx);
    if (d->cursorSurface) wl_surface_destroy(d->cursorSurface);
    if (d->cursorTheme) wl_cursor_theme_destroy(d->cursorTheme);
    if (d->surface) wl_surface_destroy(d->surface);
    if (d->xkbState) xkb_state_unref(d->xkbState);
    if (d->keymap) xkb_keymap_unref(d->keymap);
    if (d->xkb) xkb_context_unref(d->xkb);
    if (d->display) wl_display_disconnect(d->display);
}

std::string Window::error() const { return d->err; }
int Window::logicalWidth() const { return d->w; }
int Window::logicalHeight() const { return d->h; }

bool Window::open(const std::string& title, int w, int h, Handler* handler)
{
    d->handler = handler;
    d->w = w;
    d->h = h;
    d->display = wl_display_connect(nullptr);
    if (!d->display) {
        d->err = "cannot connect to a Wayland display (is WAYLAND_DISPLAY set?)";
        return false;
    }
    d->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    d->registry = wl_display_get_registry(d->display);
    wl_registry_add_listener(d->registry, &kRegistry, d.get());
    wl_display_roundtrip(d->display);
    wl_display_roundtrip(d->display);
    if (!d->compositor || !d->shm) {
        d->err = "the Wayland compositor lacks wl_compositor or wl_shm";
        return false;
    }
    d->surface = wl_compositor_create_surface(d->compositor);
    static wl_surface_listener sl;
    memset(&sl, 0, sizeof sl);
    sl.enter = surfEnter;
    sl.leave = surfLeave;
    wl_surface_add_listener(d->surface, &sl, d.get());

    static libdecor_interface di;
    memset(&di, 0, sizeof di);
    di.error = decorError;
    d->ctx = libdecor_new(d->display, &di);
    if (!d->ctx) {
        d->err = "libdecor initialisation failed";
        return false;
    }
    static libdecor_frame_interface fi;
    memset(&fi, 0, sizeof fi);
    fi.configure = frameConfigure;
    fi.close = frameClose;
    fi.commit = frameCommit;
    fi.dismiss_popup = frameDismissPopup;
    d->frame = libdecor_decorate(d->ctx, d->surface, &fi, d.get());
    if (!d->frame) {
        d->err = "libdecor could not create a window frame";
        return false;
    }
    libdecor_frame_set_app_id(d->frame, "gemway");
    libdecor_frame_set_title(d->frame, title.c_str());
    libdecor_frame_set_min_content_size(d->frame, 900, 560);
    libdecor_frame_map(d->frame);
    wl_display_flush(d->display);
    return true;
}

void Window::setTitle(const std::string& title)
{
    if (d->frame) libdecor_frame_set_title(d->frame, title.c_str());
}

void Window::quit() { d->running = false; }

int Window::run()
{
    while (d->running) {
        if ((d->dirty || d->handler->wantsRedraw()) && !d->framePending) {
            d->dirty = true;
            d->draw();
        }
        int timeout = -1;
        if (d->repeating) {
            auto now = Clock::now();
            timeout = int(std::chrono::duration_cast<std::chrono::milliseconds>(d->repeatNext - now).count());
            if (timeout < 0) timeout = 0;
        }
        if ((d->dirty || d->handler->wantsRedraw()) && d->framePending && timeout < 0) timeout = 50;
        wl_display_flush(d->display);
        if (libdecor_dispatch(d->ctx, timeout) < 0) {
            d->err = "lost connection to the Wayland display";
            return 1;
        }
        if (d->repeating && Clock::now() >= d->repeatNext) {
            d->handler->onKey(d->repeatEvent);
            d->repeatNext = Clock::now() + std::chrono::milliseconds(1000 / std::max(1, d->repeatRate));
        }
        d->handler->onIdle();
        d->updateCursor();
    }
    return 0;
}

} // namespace platform
