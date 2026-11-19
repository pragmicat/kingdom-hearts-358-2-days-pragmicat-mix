#include "drive_hud.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <memory>
#include <atomic>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace rxhud {
Model make_model(const rxdrive::View& view, bool visible) noexcept {
    Model result;
    if (!visible || !view.available || view.phase == rxdrive::Phase::Faulted) return result;
    if (view.phase == rxdrive::Phase::Active || view.phase == rxdrive::Phase::RestorationQueued) {
        if (!view.duration_ticks) return result;
        const unsigned remaining = std::min(view.remaining_ticks, view.duration_ticks);
        result.mode = view.phase == rxdrive::Phase::RestorationQueued ? Mode::RestorationQueued : Mode::Active;
        result.fraction = static_cast<float>(remaining) / static_cast<float>(view.duration_ticks);
        result.seconds = remaining / 60 + (remaining % 60 != 0 ? 1u : 0u);
        if (view.phase == rxdrive::Phase::RestorationQueued)
            std::memcpy(result.status.data(), "ENDING", 7);
        else std::snprintf(result.status.data(), result.status.size(), "%us", result.seconds);
    } else {
        if (!view.capacity) return result;
        const unsigned charge = std::min(view.charge, view.capacity);
        result.fraction = static_cast<float>(charge) / static_cast<float>(view.capacity);
        result.mode = view.ready && charge == view.capacity ? Mode::Ready : Mode::Charging;
        if (view.phase == rxdrive::Phase::ActivationQueued) {
            if (charge != view.capacity) return {};
            result.mode = Mode::ActivationQueued;
            std::memcpy(result.status.data(), "QUEUED", 7);
        } else if (result.mode == Mode::Ready) std::memcpy(result.status.data(), "READY", 6);
        else {
            const unsigned percent = static_cast<unsigned>(static_cast<std::uint64_t>(charge) * 100 / view.capacity);
            std::snprintf(result.status.data(), result.status.size(), "%u%%", percent);
        }
    }
    return result;
}
Model make_feedback_model(const rxdrive::View& view, bool visible, std::uint64_t now_ms,
                          ChargeFeedback& feedback) noexcept {
    auto model = make_model(view, visible);
    if (model.mode == Mode::Hidden) { feedback = {}; return model; }
    const unsigned charge = std::min(view.charge, view.capacity);
    const bool same_gauge = feedback.sampled && feedback.capacity == view.capacity &&
        feedback.phase == rxdrive::Phase::Normal && view.phase == rxdrive::Phase::Normal;
    if (!same_gauge || charge < feedback.charge) feedback.highlighting = false;
    if (same_gauge && charge > feedback.charge && view.capacity) {
        feedback.from = static_cast<float>(feedback.charge) / view.capacity;
        feedback.to = model.fraction;
        feedback.started_ms = now_ms;
        feedback.highlighting = true;
    }
    if (feedback.highlighting && now_ms - feedback.started_ms < 300) {
        model.gain_from = feedback.from;
        model.gain_to = feedback.to;
    } else feedback.highlighting = false;
    feedback.sampled = true; feedback.charge = charge; feedback.capacity = view.capacity;
    feedback.phase = view.phase;
    return model;
}
namespace {
Layout panel_layout(int unit) noexcept {
    Layout out;
    out.panel = {0, 0, 106 * unit, 23 * unit};
    out.label = {8 * unit, 4 * unit, 31 * unit, 8 * unit};
    out.status = {44 * unit, 4 * unit, 54 * unit, 8 * unit};
    out.track = {8 * unit, 14 * unit, 90 * unit, 5 * unit};
    out.font_px = 7 * unit;
    out.valid = true;
    return out;
}
}
Layout make_layout(int width, int height, unsigned dpi) noexcept {
    if (width < 280 || height < 180 || width > 32768 || height > 32768) return {};
    const float dpi_scale = static_cast<float>(std::clamp(dpi, 72u, 384u)) / 96.0F;
    const float fit = std::min({dpi_scale, static_cast<float>(width) / 720.0F,
                               static_cast<float>(height) / 360.0F});
    // One logical pixel always occupies a whole number of display pixels.
    // At small windows the same 5x7 letters remain legible instead of blurring.
    const int unit = std::clamp(static_cast<int>(std::lround(2.0F * fit)), 1, 8);
    Layout out = panel_layout(unit);
    const int margin = 11 * unit;
    // Reserve the native bottom-right HP area rather than covering/replacing it.
    const int bottom_reserve = std::max({50, 44 * unit, static_cast<int>(height * 0.14F)});
    out.panel.x = width - margin - out.panel.width;
    out.panel.y = std::max(margin, height - bottom_reserve - out.panel.height);
    out.valid = out.panel.x >= 0 && out.panel.y >= 0 &&
        out.panel.x + out.panel.width <= width && out.panel.y + out.panel.height <= height;
    return out;
}
bool should_show(const Model& model, bool enabled, bool visible, bool minimized, bool focused, bool fresh) noexcept {
    return model.mode != Mode::Hidden && enabled && visible && !minimized && focused && fresh;
}
namespace {
int fill_width(float fraction, int width) noexcept {
    if (width <= 0 || !std::isfinite(fraction) || fraction <= 0) return 0;
    if (fraction >= 1) return width;
    if (width == 1) return 0;
    // Reserve empty/full silhouettes for actual endpoints. Keep a positive
    // amount visible, including the final active tick, at small window sizes.
    return std::clamp(static_cast<int>(width * fraction), 1, width - 1);
}
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
struct PaintKey {
    Mode mode = Mode::Hidden;
    int fill = -1, gain_from = 0, gain_to = 0, width = 0, height = 0, font = 0;
    unsigned seconds = 0;
    std::array<char, 24> status{};
    bool operator==(const PaintKey&) const = default;
};
PaintKey paint_key(const Model& model, const Layout& layout) noexcept {
    return {model.mode, fill_width(model.fraction, layout.track.width),
            fill_width(model.gain_from, layout.track.width), fill_width(model.gain_to, layout.track.width),
            layout.panel.width, layout.panel.height, layout.font_px, model.seconds, model.status};
}
#endif
struct Color { unsigned r, g, b, a; };
void over(std::uint32_t& pixel, Color color) {
    const unsigned inverse = 255 - color.a;
    const auto channel = [color, inverse](unsigned source, unsigned dest) {
        return (source * color.a + dest * inverse + 127) / 255;
    };
    const unsigned r = channel(color.r, (pixel >> 16) & 255u);
    const unsigned g = channel(color.g, (pixel >> 8) & 255u);
    const unsigned b = channel(color.b, pixel & 255u);
    const unsigned alpha = color.a + (((pixel >> 24) * inverse + 127) / 255);
    pixel = (alpha << 24) | (r << 16) | (g << 8) | b;
}
void rectangle(Image& image, Rect box, Color color) {
    const int right = std::min(image.width, box.x + std::max(0, box.width));
    const int bottom = std::min(image.height, box.y + std::max(0, box.height));
    for (int y = std::max(0, box.y); y < bottom; ++y)
        for (int x = std::max(0, box.x); x < right; ++x)
            over(image.pixels[static_cast<std::size_t>(y) * image.width + x], color);
}
void plate(Image& image, Rect box, int cut, Color color) {
    // Hard stepped bevels retain their silhouette at the game's pixel scale.
    for (int row = 0; row < box.height; ++row) {
        const int inset = std::max({0, cut - row, cut - (box.height - row - 1)});
        rectangle(image, {box.x + inset, box.y + row, box.width - 2 * inset, 1}, color);
    }
}
// Original 5x7 glyphs authored for this HUD. No external font or game asset is
// loaded. The same raster reaches Windows and headless previews byte-for-byte.
std::array<unsigned char, 7> glyph(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    switch (c) {
    case 'A': return {14,17,17,31,17,17,17};
    case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {15,16,16,16,16,16,15};
    case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31};
    case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {15,16,16,23,17,17,15};
    case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {31,4,4,4,4,4,31};
    case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17};
    case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17};
    case 'N': return {17,25,25,21,19,19,17};
    case 'O': return {14,17,17,17,17,17,14};
    case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13};
    case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30};
    case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14};
    case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,27,17};
    case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4};
    case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14};
    case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31};
    case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2};
    case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14};
    case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14};
    case '9': return {14,17,17,15,1,1,14};
    case '%': return {25,25,2,4,8,19,19};
    case '-': return {0,0,0,31,0,0,0};
    case '.': return {0,0,0,0,0,12,12};
    case ' ': return {};
    default: return {14,17,1,2,4,0,4};
    }
}
bool text(Image& image, Rect box, int font_px, const char* value, bool right, Color color) {
    if (!value || !*value) return true;
    std::size_t length = 0;
    while (length < 24 && value[length]) ++length;
    if (length == 24 || box.width < 1 || box.height < 8) return false;
    const int units = static_cast<int>(length) * 6 - 1;
    const int unit = std::min({font_px / 7, box.height / 8, box.width / (units + 1)});
    if (unit < 1) return false;
    const int origin_x = right ? box.x + box.width - (units + 1) * unit : box.x;
    const int origin_y = box.y + (box.height - 8 * unit) / 2;
    // Shadow is drawn first, then opaque foreground so neighboring glyphs do
    // not dim one another. Neither layer needs GDI or antialiasing coverage.
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t n = 0; n < length; ++n) {
            const auto rows = glyph(value[n]);
            const int x = origin_x + static_cast<int>(n) * 6 * unit + (pass == 0 ? unit : 0);
            const int y = origin_y + (pass == 0 ? unit : 0);
            for (int row = 0; row < 7; ++row) for (int column = 0; column < 5; ++column)
                if (rows[row] & (1u << (4 - column)))
                    rectangle(image, {x + column * unit, y + row * unit, unit, unit},
                              pass == 0 ? Color{0,3,8,255} : color);
        }
    return true;
}
#ifdef _WIN32
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    void* data = nullptr;
    int width = 0, height = 0;
    void reset() noexcept {
        if (dc && previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        dc = nullptr; bitmap = nullptr; previous = nullptr; data = nullptr; width = height = 0;
    }
    bool create(int w, int h) noexcept {
        if (dc && width == w && height == h) return true;
        reset();
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        if (dc) bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!dc || !bitmap || !data) { reset(); return false; }
        previous = SelectObject(dc, bitmap);
        if (!previous || previous == HGDI_ERROR) { previous = nullptr; reset(); return false; }
        width = w; height = h;
        return true;
    }
    ~Surface() noexcept { reset(); }
};
#endif
}
Image render(const Model& model, const Layout& layout) noexcept {
    try {
    Image image;
    if (!layout.valid || model.mode == Mode::Hidden || layout.panel.width <= 0 || layout.panel.height <= 0 ||
        layout.panel.width > 2048 || layout.panel.height > 512 ||
        layout.font_px < 7 || layout.font_px > 56 || layout.font_px % 7) return image;
    const auto fits = [&](Rect box) {
        return box.x >= 0 && box.y >= 0 && box.width > 0 && box.height > 0 &&
            box.x <= layout.panel.width && box.y <= layout.panel.height &&
            box.width <= layout.panel.width - box.x && box.height <= layout.panel.height - box.y;
    };
    if (!fits(layout.label) || !fits(layout.status) || !fits(layout.track)) return image;
    image.width = layout.panel.width; image.height = layout.panel.height;
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    const int unit = std::max(1, layout.font_px / 7);
    const Rect frame{unit, unit, image.width - 3 * unit, image.height - 3 * unit};
    plate(image, {frame.x + unit, frame.y + unit, frame.width, frame.height}, 3 * unit, {0,3,8,180});
    plate(image, frame, 3 * unit, {116,149,171,255});
    plate(image, {frame.x + unit, frame.y + unit, frame.width - 2 * unit, frame.height - 2 * unit},
          2 * unit, {8,19,31,245});
    rectangle(image, {7 * unit, 2 * unit, image.width - 15 * unit, unit}, {175,201,214,255});
    const bool entering = model.mode == Mode::ActivationQueued;
    const bool leaving = model.mode == Mode::RestorationQueued;
    const bool gold = model.mode == Mode::Ready || entering;
    const bool low_time = model.mode == Mode::Active && model.seconds > 0 && model.seconds <= 3;
    const Color accent = gold ? Color{255,205,83,255} :
                         leaving ? Color{179,204,220,255} : low_time ? Color{255,163,70,255} :
                         model.mode == Mode::Active ? Color{85,165,255,255} : Color{65,210,238,255};
    rectangle(image, {3 * unit, 7 * unit, 2 * unit, 8 * unit}, accent);
    const auto track = layout.track;
    rectangle(image, {track.x - unit, track.y - unit, track.width + 2 * unit, track.height + 2 * unit}, {68,98,121,255});
    rectangle(image, track, {3,10,18,255});
    const int filled = fill_width(model.fraction, track.width);
    if (filled > 0) {
        rectangle(image, {track.x, track.y, filled, track.height}, accent);
        rectangle(image, {track.x, track.y, filled, unit},
                  gold ? Color{255,237,166,255} :
                  leaving ? Color{217,233,241,255} : low_time ? Color{255,215,151,255} : Color{160,232,255,255});
        rectangle(image, {track.x, track.y + track.height - unit, filled, unit},
                  {accent.r / 2, accent.g / 2, accent.b / 2,255});
    }
    if (std::isfinite(model.gain_from) && std::isfinite(model.gain_to) && model.gain_to > model.gain_from) {
        const int begin = std::min(filled, fill_width(model.gain_from, track.width));
        const int end = std::min(filled, fill_width(model.gain_to, track.width));
        if (end > begin)
            rectangle(image, {track.x + begin, track.y + unit, end - begin, track.height - 2 * unit},
                      gold ? Color{255,241,183,255} : Color{204,248,255,255});
    }
    // A pending form change gets directional dividers immediately, even if an
    // ongoing attack holds up the swap. Leave the surviving fill edge intact.
    for (int cell = 1; cell < 10; ++cell) {
        const int divider = cell * track.width / 10 - unit;
        if (!entering && !leaving) {
            rectangle(image, {track.x + divider, track.y, unit, track.height}, {8,19,31,255});
            continue;
        }
        for (int row = 0; row < 5; ++row) {
            const int bend = (2 - std::abs(2 - row)) * unit;
            const int x = divider + (entering ? bend : -bend);
            // Keep empty chevrons visible after natural expiry, without lighting
            // any part of the track as remaining time.
            const int on_fill = std::clamp(filled - x, 0, unit);
            rectangle(image, {track.x + x, track.y + row * unit, on_fill, unit}, {8,19,31,255});
            rectangle(image, {track.x + x + on_fill, track.y + row * unit, unit - on_fill, unit},
                      {80,108,126,255});
        }
    }
    if (!text(image, layout.label, layout.font_px, "DRIVE", false, {226,240,246,255}) ||
        !text(image, layout.status, layout.font_px, model.status.data(), true,
              (gold || leaving || low_time) ? accent : Color{226,240,246,255})) return {};
    return image;
    } catch (...) {
        // This optional HUD must never abort native combat for an allocation or
        // raster failure. The worker hides an empty image and retries later.
        return {};
    }
}

#ifdef _WIN32
namespace {
constexpr wchar_t kWindowClass[] = L"KingdomHearts358Days.PragmicatMix.DriveHUD.v1";
constexpr wchar_t kGameTitle[] = L"Kingdom Hearts 358/2 Days: Pragmicat Mix";
constexpr ULONGLONG kStaleMs = 500;
struct Shared {
    SRWLOCK mutex = SRWLOCK_INIT;
    Model model;
    ChargeFeedback feedback;
    bool enabled = true;
    std::atomic<bool> ready_ok{false};
    ULONGLONG updated = 0;
    HANDLE wake = nullptr, end = nullptr, ready = nullptr, thread = nullptr;
    DWORD thread_id = 0;
    ~Shared() noexcept {
        if (thread) {
            if (end) SetEvent(end);
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
        }
        if (wake) CloseHandle(wake);
        if (end) CloseHandle(end);
        if (ready) CloseHandle(ready);
    }
};
struct SrwGuard {
    SRWLOCK& lock;
    explicit SrwGuard(SRWLOCK& value) noexcept : lock(value) { AcquireSRWLockExclusive(&lock); }
    ~SrwGuard() noexcept { ReleaseSRWLockExclusive(&lock); }
};
SRWLOCK lifecycle = SRWLOCK_INIT;
std::unique_ptr<Shared> shared;

using SetDpiContext = HANDLE (WINAPI*)(HANDLE);
using GetWindowDpi = UINT (WINAPI*)(HWND);
struct DpiScope {
    SetDpiContext set = nullptr;
    HANDLE previous = nullptr;
    DpiScope() {
        const HMODULE user = GetModuleHandleW(L"user32.dll");
        set = user ? reinterpret_cast<SetDpiContext>(GetProcAddress(user, "SetThreadDpiAwarenessContext")) : nullptr;
        if (set) previous = set(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))); // per-monitor-aware-v2
    }
    ~DpiScope() { if (set && previous) set(previous); }
};
unsigned window_dpi(HWND window) {
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    const auto get = user ? reinterpret_cast<GetWindowDpi>(GetProcAddress(user, "GetDpiForWindow")) : nullptr;
    return get ? std::max(96u, get(window)) : 96u;
}
bool identity_candidate(DWORD process, bool owned, LONG_PTR style, LONG_PTR extended, const wchar_t* name) noexcept {
    return process == GetCurrentProcessId() && !owned && !(style & WS_CHILD) &&
        !(extended & WS_EX_TOOLWINDOW) && name && std::wcscmp(name, L"SDL_app") == 0;
}
bool native_window(HWND window) {
    DWORD process = 0; GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return false;
    wchar_t name[64]{};
    return GetClassNameW(window, name, static_cast<int>(std::size(name))) &&
        identity_candidate(process, GetWindow(window, GW_OWNER) != nullptr,
            GetWindowLongPtrW(window, GWL_STYLE), GetWindowLongPtrW(window, GWL_EXSTYLE), name);
}
bool bounded_message(HWND window, UINT message, WPARAM parameter, LPARAM data, DWORD_PTR& result) noexcept {
    return SendMessageTimeoutW(window, message, parameter, data,
        SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 50, &result) != 0;
}
int stored_caption(HWND window, wchar_t* buffer, int capacity) noexcept {
    // Read the stored caption directly; GetWindowText on another thread would
    // send an unbounded WM_GETTEXT to SDL's owner thread.
    using ReadCaption = int (WINAPI*)(HWND, LPWSTR, int);
    const auto user = GetModuleHandleW(L"user32.dll");
    const auto read = user ? reinterpret_cast<ReadCaption>(GetProcAddress(user, "InternalGetWindowText")) : nullptr;
    return read ? read(window, buffer, capacity) : 0;
}
bool bounded_caption(HWND window, const wchar_t* text) noexcept {
    const SIZE_T bytes = (std::wcslen(text) + 1) * sizeof(wchar_t);
    auto* copy = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), 0, bytes));
    if (!copy) return false;
    std::memcpy(copy, text, bytes);
    DWORD_PTR result = 0;
    const bool completed = bounded_message(window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(copy), result);
    // A timed-out receiver may still use lParam. Retain that small process-heap
    // buffer until process exit, so unloading this DLL cannot invalidate it.
    if (completed) HeapFree(GetProcessHeap(), 0, copy);
    return completed && result != 0;
}
HICON owned_icon(HMODULE module, int pixels) noexcept {
    // Independent copies remain valid if SDL is unresponsive during DLL unload.
    return static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON,
        pixels, pixels, 0));
}
bool window_exists(HWND window) noexcept { return IsWindow(window) != 0; }
void destroy_icon(HICON icon) noexcept { if (icon) DestroyIcon(icon); }
struct IdentityApi {
    bool (*valid)(HWND) = &native_window;
    bool (*exists)(HWND) = &window_exists;
    int (*caption)(HWND, wchar_t*, int) = &stored_caption;
    HICON (*icon)(HMODULE, int) = &owned_icon;
    void (*destroy)(HICON) = &destroy_icon;
    bool (*message)(HWND, UINT, WPARAM, LPARAM, DWORD_PTR&) = &bounded_message;
    bool (*title)(HWND, const wchar_t*) = &bounded_caption;
};
struct WindowIdentity {
    IdentityApi api;
    HWND window = nullptr;
    HICON small = nullptr, large = nullptr, old_small = nullptr, old_large = nullptr;
    bool changed_small = false, changed_large = false, changed_title = false;
    bool pending_small = false, pending_large = false;
    std::array<wchar_t, 256> old_title{};
    explicit WindowIdentity(IdentityApi value = {}) noexcept : api(value) {}
    bool restore_icon(WPARAM kind, HICON ours, HICON previous, bool changed, bool pending) noexcept {
        if (!ours || !window || !api.exists(window) || !changed) return true;
        if (!api.valid(window)) return false;
        DWORD_PTR current = 0, ignored = 0;
        if (!api.message(window, WM_GETICON, kind, 0, current)) return false;
        if (reinterpret_cast<HICON>(current) != ours) return !pending;
        return api.message(window, WM_SETICON, kind, reinterpret_cast<LPARAM>(previous), ignored) && !pending;
    }
    void reset() noexcept {
        const bool release_small = restore_icon(ICON_SMALL, small, old_small, changed_small, pending_small);
        const bool release_large = restore_icon(ICON_BIG, large, old_large, changed_large, pending_large);
        if (window && api.valid(window)) {
            std::array<wchar_t, 256> current{};
            if (changed_title && api.caption(window, current.data(), static_cast<int>(current.size())) > 0 &&
                std::wcscmp(current.data(), kGameTitle) == 0)
                api.title(window, old_title.data());
        }
        // Keep a possibly in-use copy until process exit after an actual timeout.
        // No icon then points into this DLL, and normal cleanup releases both.
        if (release_small) api.destroy(small);
        if (release_large) api.destroy(large);
        small = large = nullptr; pending_small = pending_large = false;
        window = nullptr; changed_small = changed_large = changed_title = false;
    }
    void apply_icon(WPARAM kind, HICON ours, HICON& previous, bool& changed, bool& pending) noexcept {
        DWORD_PTR current = 0, actual_previous = 0;
        if (!ours || !api.message(window, WM_GETICON, kind, 0, current) || reinterpret_cast<HICON>(current) == ours) return;
        previous = reinterpret_cast<HICON>(current); changed = true;
        if (api.message(window, WM_SETICON, kind, reinterpret_cast<LPARAM>(ours), actual_previous))
            previous = reinterpret_cast<HICON>(actual_previous);
        else pending = true;
    }
    void attach(HWND candidate, HMODULE module) noexcept {
        if (window == candidate) return;
        reset();
        if (!candidate || !api.valid(candidate)) return;
        window = candidate;
        small = api.icon(module, GetSystemMetrics(SM_CXSMICON));
        large = api.icon(module, GetSystemMetrics(SM_CXICON));
        apply_icon(ICON_SMALL, small, old_small, changed_small, pending_small);
        apply_icon(ICON_BIG, large, old_large, changed_large, pending_large);
        const int length = api.caption(window, old_title.data(), static_cast<int>(old_title.size()));
        if (length > 0 && length < static_cast<int>(old_title.size()) - 1 && std::wcscmp(old_title.data(), kGameTitle) != 0) {
            changed_title = true;
            api.title(window, kGameTitle);
        }
    }
    ~WindowIdentity() noexcept { reset(); }
};
struct Candidate { HWND window = nullptr; long long area = 0; };
BOOL CALLBACK find_window(HWND window, LPARAM parameter) noexcept {
    if (!native_window(window) || !IsWindowVisible(window)) return TRUE;
    RECT rect{};
    if (!GetClientRect(window, &rect) || rect.right < 280 || rect.bottom < 180) return TRUE;
    // The source runtime creates one main SDL_app window. PID/class/ownership
    // checks avoid all WM_GETTEXT/SendMessage calls to its separate SDL thread.
    auto& candidate = *reinterpret_cast<Candidate*>(parameter);
    const long long area = static_cast<long long>(rect.right) * rect.bottom;
    if (area > candidate.area) candidate = {window, area};
    return TRUE;
}
HWND game_window() { Candidate candidate; EnumWindows(&find_window, reinterpret_cast<LPARAM>(&candidate)); return candidate.window; }
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: ValidateRect(window, nullptr); return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}
DWORD run_ui(Shared& state) {
    DpiScope dpi_context;
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&window_proc), &module);
    WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = module;
    type.lpfnWndProc = &window_proc; type.lpszClassName = kWindowClass;
    const ATOM atom = module ? RegisterClassExW(&type) : 0;
    HWND game = nullptr, hud = nullptr;
    WindowIdentity identity;
    Surface surface;
    struct Cleanup {
        HMODULE module;
        ATOM atom;
        HWND& hud;
        Surface& surface;
        ~Cleanup() noexcept {
            if (hud && IsWindow(hud)) DestroyWindow(hud);
            surface.reset();
            if (atom) UnregisterClassW(kWindowClass, module);
        }
    } cleanup{module, atom, hud, surface};
    state.ready_ok = atom != 0; SetEvent(state.ready);
    if (!atom) return 1;
    PaintKey last;
    bool painted = false;
    const HANDLE events[] = {state.end, state.wake};
    bool done = false;
    while (!done) {
        const DWORD result = MsgWaitForMultipleObjects(2, events, FALSE, 100, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { done = true; break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (done) break;
        Model model; bool enabled; ULONGLONG updated;
        { SrwGuard lock(state.mutex); model = state.model; enabled = state.enabled; updated = state.updated; }
        if (!game || !IsWindow(game) || !native_window(game)) {
            if (hud && IsWindow(hud)) DestroyWindow(hud);
            hud = nullptr; painted = false; game = game_window();
        }
        identity.attach(game, module);
        const bool focus = game && GetForegroundWindow() == game;
        if (!game || !should_show(model, enabled, IsWindowVisible(game) != 0, IsIconic(game) != 0,
                                  focus, updated && GetTickCount64() - updated <= kStaleMs)) {
            if (hud && IsWindowVisible(hud)) ShowWindow(hud, SW_HIDE);
            continue;
        }
        RECT client{}; POINT origin{};
        if (!GetClientRect(game, &client) || !ClientToScreen(game, &origin)) {
            if (hud) ShowWindow(hud, SW_HIDE); continue;
        }
        const Layout layout = make_layout(client.right, client.bottom, window_dpi(game));
        if (!layout.valid) { if (hud) ShowWindow(hud, SW_HIDE); continue; }
        if (!hud || !IsWindow(hud)) {
            hud = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                kWindowClass, L"Kingdom Hearts 358/2 Days: Pragmicat Mix — Drive", WS_POPUP, 0, 0, 1, 1, game, nullptr, module, nullptr);
            painted = false;
            if (!hud) continue;
        }
        const PaintKey key = paint_key(model, layout);
        if (!painted || !(key == last)) {
            const Image image = render(model, layout);
            if (image.pixels.empty() || !surface.create(image.width, image.height)) { ShowWindow(hud, SW_HIDE); continue; }
            std::memcpy(surface.data, image.pixels.data(), image.pixels.size() * sizeof(std::uint32_t));
            last = key; painted = true;
        }
        POINT destination{origin.x + layout.panel.x, origin.y + layout.panel.y}, source{};
        SIZE size{layout.panel.width, layout.panel.height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(hud, nullptr, &destination, &size, surface.dc, &source, 0, &blend, ULW_ALPHA)) {
            ShowWindow(hud, SW_HIDE); continue;
        }
        SetWindowPos(hud, HWND_TOP, 0, 0, 0, 0,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
    }
    return 0;
}
DWORD WINAPI ui_thread(void* parameter) noexcept {
    auto& state = *static_cast<Shared*>(parameter);
    try {
        return run_ui(state);
    } catch (...) {
        // run_ui's RAII cleanup has already hidden/destroyed its HUD, selected
        // bitmap/DC and registered class. Unblock start() even on startup error.
        state.ready_ok = false; SetEvent(state.ready);
        return 1;
    }
}
}
bool start(bool enabled) noexcept {
    try {
    SrwGuard lock(lifecycle);
    if (shared && WaitForSingleObject(shared->thread, 0) == WAIT_TIMEOUT) return true;
    shared.reset(); // A stopped/failed worker can be explicitly started again.
    auto next = std::make_unique<Shared>(); next->enabled = enabled;
    next->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    next->end = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    next->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!next->wake || !next->end || !next->ready) return false;
    next->thread = CreateThread(nullptr, 0, &ui_thread, next.get(), 0, &next->thread_id);
    if (!next->thread) return false;
    if (WaitForSingleObject(next->ready, 2000) != WAIT_OBJECT_0 || !next->ready_ok) {
        return false; // Shared's destructor signals, joins and closes all handles.
    }
    shared = std::move(next); return true;
    } catch (...) { return false; }
}
void update(const rxdrive::View& view, bool visible) noexcept {
    try {
    SrwGuard lock(lifecycle);
    if (!shared) return;
    { SrwGuard state_lock(shared->mutex);
      shared->updated = GetTickCount64();
      shared->model = make_feedback_model(view, visible, shared->updated, shared->feedback); }
    SetEvent(shared->wake);
    } catch (...) {} // Never cross a native frame callback boundary.
}
void set_enabled(bool enabled) noexcept {
    try {
    SrwGuard lock(lifecycle);
    if (!shared) return;
    { SrwGuard state_lock(shared->mutex); shared->enabled = enabled; }
    SetEvent(shared->wake);
    } catch (...) {}
}
void stop() noexcept {
    try {
    SrwGuard lock(lifecycle);
    if (!shared) return;
    if (GetCurrentThreadId() == shared->thread_id) { SetEvent(shared->end); return; }
    shared.reset(); // join occurs without requiring either lock on the worker
    } catch (...) {}
}
#elif defined(__EMSCRIPTEN__)
namespace {
// The presenter copies this transient raster synchronously, before Wasm can
// grow its memory. Null pixels clear the gauge. Zero height removes its strip.
EM_JS(void, publish_browser_hud, (const std::uint32_t* pixels, int width, int height), {
    if (typeof Module["onDaysDriveHud"] === "function")
        Module["onDaysDriveHud"](pixels, width, height);
});
const Layout browser_layout = panel_layout(1);
ChargeFeedback browser_feedback;
PaintKey last_paint;
bool browser_enabled, browser_painted;
}
bool start(bool enabled) noexcept {
    set_enabled(enabled);
    return true;
}
void update(const rxdrive::View& view, bool visible) noexcept {
    if (!browser_enabled) return;
    const auto model = make_feedback_model(view, visible, static_cast<std::uint64_t>(emscripten_get_now()),
                                           browser_feedback);
    const auto& panel = browser_layout.panel;
    if (model.mode == Mode::Hidden) {
        if (browser_painted) publish_browser_hud(nullptr, panel.width, panel.height);
        browser_painted = false;
        return;
    }
    const auto key = paint_key(model, browser_layout);
    if (browser_painted && key == last_paint) return;
    const auto image = render(model, browser_layout);
    publish_browser_hud(image.pixels.empty() ? nullptr : image.pixels.data(), panel.width, panel.height);
    browser_painted = !image.pixels.empty();
    last_paint = key;
}
void set_enabled(bool enabled) noexcept {
    browser_enabled = enabled;
    browser_painted = false;
    browser_feedback = {};
    publish_browser_hud(nullptr, enabled ? browser_layout.panel.width : 0,
                        enabled ? browser_layout.panel.height : 0);
}
void stop() noexcept { set_enabled(false); }
#else
bool start(bool) noexcept { return false; }
void update(const rxdrive::View&, bool) noexcept {}
void set_enabled(bool) noexcept {}
void stop() noexcept {}
#endif
}
