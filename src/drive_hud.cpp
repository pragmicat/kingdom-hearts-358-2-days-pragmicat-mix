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
#endif

namespace rxhud {
Model make_model(const rxdrive::View& view, bool visible) noexcept {
    Model result;
    if (!visible || !view.available || view.phase == rxdrive::Phase::Faulted) return result;
    if (view.phase == rxdrive::Phase::Active || view.phase == rxdrive::Phase::RestorationQueued) {
        if (!view.duration_ticks) return result;
        const unsigned remaining = std::min(view.remaining_ticks, view.duration_ticks);
        result.mode = Mode::Active;
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
            result.mode = Mode::Ready;
            std::memcpy(result.status.data(), "QUEUED", 7);
        } else if (result.mode == Mode::Ready) std::memcpy(result.status.data(), "READY", 6);
        else {
            const unsigned percent = static_cast<unsigned>(static_cast<std::uint64_t>(charge) * 100 / view.capacity);
            std::snprintf(result.status.data(), result.status.size(), "%u%%", percent);
        }
    }
    return result;
}
Layout make_layout(int width, int height, unsigned dpi) noexcept {
    Layout out;
    if (width < 280 || height < 180 || width > 32768 || height > 32768) return out;
    const float pixel_scale = static_cast<float>(std::clamp(dpi, 72u, 384u)) / 96.0F;
    const float fit = std::min({pixel_scale, static_cast<float>(width) / 720.0F,
                               static_cast<float>(height) / 360.0F});
    const float scale = std::max(0.65F, fit);
    const auto px = [scale](float value) { return std::max(1, static_cast<int>(std::lround(value * scale))); };
    const int margin = px(22), panel_width = std::min(px(224), width - margin * 2);
    const int panel_height = px(58);
    // Reserve the native bottom-right HP area rather than covering/replacing it.
    const int bottom_reserve = std::max(px(88), static_cast<int>(height * 0.14F));
    out.panel = {width - margin - panel_width, std::max(margin, height - bottom_reserve - panel_height),
                 panel_width, panel_height};
    const int inset = px(14), top = px(8), text_height = px(20);
    out.label = {inset, top, px(70), text_height};
    out.status = {px(84), top, panel_width - px(84) - inset, text_height};
    out.track = {inset, px(35), panel_width - inset * 2, px(7)};
    out.font_px = px(12);
    out.valid = out.panel.x >= 0 && out.panel.y >= 0 &&
        out.panel.x + panel_width <= width && out.panel.y + panel_height <= height &&
        out.track.width > 0 && out.status.width > 0;
    return out;
}
bool should_show(const Model& model, bool enabled, bool visible, bool minimized, bool focused, bool fresh) noexcept {
    return model.mode != Mode::Hidden && enabled && visible && !minimized && focused && fresh;
}
namespace {
struct Color { unsigned r, g, b, a; };
void over(std::uint32_t& pixel, Color color, float coverage) {
    const unsigned a = static_cast<unsigned>(std::clamp(std::lround(color.a * coverage), 0l, 255l));
    const unsigned inverse = 255 - a;
    const auto channel = [a, inverse](unsigned source, unsigned dest) {
        return (source * a + dest * inverse + 127) / 255;
    };
    const unsigned r = channel(color.r, (pixel >> 16) & 255u);
    const unsigned g = channel(color.g, (pixel >> 8) & 255u);
    const unsigned b = channel(color.b, pixel & 255u);
    const unsigned alpha = a + (((pixel >> 24) * inverse + 127) / 255);
    pixel = (alpha << 24) | (r << 16) | (g << 8) | b;
}
void round_rect(Image& image, Rect box, float radius, Color color) {
    if (box.width <= 0 || box.height <= 0) return;
    radius = std::clamp(radius, 0.0F, std::min(box.width, box.height) * 0.5F);
    const float half_x = box.width * 0.5F, half_y = box.height * 0.5F;
    for (int y = std::max(0, box.y - 1); y < std::min(image.height, box.y + box.height + 1); ++y)
        for (int x = std::max(0, box.x - 1); x < std::min(image.width, box.x + box.width + 1); ++x) {
            const float qx = std::abs(x + 0.5F - box.x - half_x) - half_x + radius;
            const float qy = std::abs(y + 0.5F - box.y - half_y) - half_y + radius;
            const float distance = std::sqrt(std::max(qx, 0.0F) * std::max(qx, 0.0F) +
                                             std::max(qy, 0.0F) * std::max(qy, 0.0F)) +
                                   std::min(std::max(qx, qy), 0.0F) - radius;
            over(image.pixels[static_cast<std::size_t>(y) * image.width + x], color,
                 std::clamp(0.5F - distance, 0.0F, 1.0F));
        }
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
bool text(Image& image, Rect box, int font_px, const char* value, bool right, Color color) {
    if (!value || !*value) return true;
    Surface mask;
    if (!mask.create(image.width, image.height)) return false;
    std::memset(mask.data, 0, image.pixels.size() * sizeof(std::uint32_t));
    const HFONT font = CreateFontW(-font_px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!font) return false;
    const HGDIOBJ previous = SelectObject(mask.dc, font);
    if (!previous || previous == HGDI_ERROR) { DeleteObject(font); return false; }
    SetBkMode(mask.dc, TRANSPARENT); SetTextColor(mask.dc, RGB(255, 255, 255));
    std::array<wchar_t, 32> wide{};
    for (std::size_t i = 0; value[i] && i + 1 < wide.size(); ++i) wide[i] = static_cast<unsigned char>(value[i]);
    RECT bounds{box.x, box.y, box.x + box.width, box.y + box.height};
    const bool drawn = DrawTextW(mask.dc, wide.data(), -1, &bounds,
        DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (right ? DT_RIGHT : DT_LEFT)) != 0;
    const bool flushed = GdiFlush() != 0; // synchronize GDI before reading DIB coverage
    const auto* pixels = static_cast<const std::uint32_t*>(mask.data);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const unsigned coverage = std::max({pixels[i] & 255u, (pixels[i] >> 8) & 255u, (pixels[i] >> 16) & 255u});
        if (coverage) over(image.pixels[i], color, coverage / 255.0F);
    }
    SelectObject(mask.dc, previous); DeleteObject(font);
    return drawn && flushed;
}
#endif
}
Image render(const Model& model, const Layout& layout) noexcept {
    try {
    Image image;
    if (!layout.valid || model.mode == Mode::Hidden || layout.panel.width <= 0 || layout.panel.height <= 0 ||
        layout.panel.width > 2048 || layout.panel.height > 512) return image;
    image.width = layout.panel.width; image.height = layout.panel.height;
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    const float scale = image.height / 58.0F;
    const int edge = std::max(1, static_cast<int>(std::lround(2 * scale)));
    const Rect card{edge, edge, image.width - edge * 2, image.height - edge * 2 - 1};
    round_rect(image, {card.x + 1, card.y + 2, card.width, card.height}, 10 * scale, {0, 0, 0, 70});
    round_rect(image, card, 10 * scale, {102, 136, 164, 110});
    round_rect(image, {card.x + 1, card.y + 1, card.width - 2, card.height - 2}, 9 * scale, {7, 16, 27, 240});
    Color accent = model.mode == Mode::Ready ? Color{255, 204, 99, 255} :
                   model.mode == Mode::Active ? Color{91, 222, 255, 255} : Color{83, 177, 210, 245};
    const auto track = layout.track;
    round_rect(image, track, track.height * 0.5F, {18, 35, 49, 255});
    const float fraction = std::isfinite(model.fraction) ? std::clamp(model.fraction, 0.0F, 1.0F) : 0.0F;
    const int filled = static_cast<int>(std::lround(track.width * fraction));
    if (filled > 0) {
        const Rect fill{track.x, track.y, filled, track.height};
        if (model.mode == Mode::Ready)
            round_rect(image, {fill.x - 1, fill.y - 1, fill.width + 2, fill.height + 2}, static_cast<float>(track.height), {255, 190, 65, 58});
        round_rect(image, fill, track.height * 0.5F, accent);
        if (track.height >= 4)
            round_rect(image, {fill.x + 2, fill.y + 1, std::max(0, fill.width - 4), 1}, 0, {255, 255, 255, 95});
    }
#ifdef _WIN32
    if (!text(image, layout.label, layout.font_px, "DRIVE", false, {194, 212, 226, 205}) ||
        !text(image, layout.status, layout.font_px, model.status.data(), true,
              model.mode == Mode::Ready ? accent : Color{218, 239, 249, 245})) return {};
#endif
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
struct PaintKey {
    Mode mode = Mode::Hidden;
    int fill = -1, width = 0, height = 0, font = 0;
    unsigned seconds = 0;
    std::array<char, 24> status{};
    bool operator==(const PaintKey&) const = default;
};
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
        const PaintKey key{model.mode, static_cast<int>(std::lround(model.fraction * layout.track.width)),
                           layout.panel.width, layout.panel.height, layout.font_px, model.seconds, model.status};
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
    { SrwGuard state_lock(shared->mutex); shared->model = make_model(view, visible); shared->updated = GetTickCount64(); }
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
#else
bool start(bool) noexcept { return false; }
void update(const rxdrive::View&, bool) noexcept {}
void set_enabled(bool) noexcept {}
void stop() noexcept {}
#endif
}
