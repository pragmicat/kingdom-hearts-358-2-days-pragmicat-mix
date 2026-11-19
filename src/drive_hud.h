#pragma once
#include "drive_form.h"
#include <array>
#include <cstdint>
#include <vector>

namespace rxhud {
enum class Mode { Hidden, Charging, Ready, ActivationQueued, Active, RestorationQueued };
struct Rect { int x = 0, y = 0, width = 0, height = 0; };
struct Layout {
    Rect panel, label, status, track; // panel is client-relative; others are local
    int font_px = 0;
    bool valid = false;
};
struct Model {
    Mode mode = Mode::Hidden;
    float fraction = 0;
    unsigned seconds = 0;
    std::array<char, 24> status{};
    float gain_from = 0, gain_to = 0; // brief highlight over newly earned charge only
};
struct ChargeFeedback {
    bool sampled = false, highlighting = false;
    unsigned charge = 0, capacity = 0;
    rxdrive::Phase phase = rxdrive::Phase::Normal;
    std::uint64_t started_ms = 0;
    float from = 0, to = 0;
};
struct Image {
    int width = 0, height = 0;
    // Top-down premultiplied BGRA, suitable for UpdateLayeredWindow/AC_SRC_ALPHA.
    std::vector<std::uint32_t> pixels;
};
Model make_model(const rxdrive::View&, bool visible) noexcept;
// Presentation only: no game-memory, resource or input changes. Hiding the HUD
// resets the baseline so pause/resume and scene entry do not imitate earned hits.
Model make_feedback_model(const rxdrive::View&, bool visible, std::uint64_t now_ms,
                          ChargeFeedback&) noexcept;
Layout make_layout(int client_width, int client_height, unsigned dpi = 96) noexcept;
bool should_show(const Model&, bool enabled, bool owner_visible,
                 bool owner_minimized, bool owner_foreground, bool fresh) noexcept;
// Portable pixel rasterization with original 5x7 glyphs and whole-pixel scaling.
// No font/GDI dependency; offscreen rendering never creates or displays a window.
Image render(const Model&, const Layout&) noexcept;

// Starts a dedicated message-pumping UI thread. A HUD window is created lazily
// only after an eligible current-process native SDL game window is found.
bool start(bool enabled = true) noexcept;
// Copies ONLY this supplied Drive view; no game-memory reads or input calls.
// Safe to call once per native frame. Hidden for pauses/cinematics via visible.
void update(const rxdrive::View&, bool visible) noexcept;
void set_enabled(bool enabled) noexcept;
// Signals, joins and destroys the owned UI thread/window/GDI resources.
void stop() noexcept;
}
