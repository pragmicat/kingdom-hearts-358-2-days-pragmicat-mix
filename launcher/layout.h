#pragma once
#include <algorithm>
#include <cmath>

namespace zw::ui {
struct Rect { int x = 0, y = 0, width = 0, height = 0; };
struct ClientSize { int width = 0, height = 0; };
struct Layout {
    Rect logo, heading, intro, campaign, practice, status, progress, browse, version;
    float scale = 1;
    bool show_logo = false, valid = false;
};
// Standard dialog OK/Cancel identifiers are1/2. Buttons10..12 are launcher actions.
inline int resolve_command(int command, int default_id, bool busy) {
    if (command == 2) return 2;
    if (busy) return 0;
    const int selected = command == 1 ? default_id : command;
    return selected >= 10 && selected <= 12 ? selected : 0;
}
inline ClientSize fit_client(int work_width, int work_height, unsigned dpi,
                             int chrome_width = 0, int chrome_height = 0, bool minimum = false) {
    const double scale = std::clamp(dpi, 72u, 384u) / 96.0;
    const int margin = std::max(12, static_cast<int>(std::lround(12 * scale)));
    const int available_width = std::max(1, work_width - chrome_width - margin * 2);
    const int available_height = std::max(1, work_height - chrome_height - margin * 2);
    return {std::min(available_width, static_cast<int>(std::lround((minimum ? 480 : 640) * scale))),
            std::min(available_height, static_cast<int>(std::lround((minimum ? 440 : 568) * scale)))};
}
inline Layout calculate(int width, int height, unsigned dpi) {
    Layout out;
    if (width < 320 || height < 300 || width > 32768 || height > 32768) return out;
    out.scale = static_cast<float>(std::min({std::clamp(dpi, 72u, 384u) / 96.0,
                                            width / 480.0, height / 440.0}));
    const auto px = [&](int n) { return std::max(1, static_cast<int>(std::lround(n * out.scale))); };
    const int margin = px(24), gap = px(12), inner = width - margin * 2;
    int bottom = height - margin;
    out.version = {margin, bottom - px(20), inner, px(20)};
    bottom = out.version.y - px(8);
    out.browse = {margin, bottom - px(32), std::min(inner, px(200)), px(32)};
    bottom = out.browse.y - gap;
    out.progress = {margin, bottom - px(5), inner, px(5)};
    bottom = out.progress.y - gap;
    out.status = {margin, bottom - px(72), inner, px(72)};
    bottom = out.status.y - gap;
    if (inner >= px(480)) {
        const int first = (inner - gap) / 2;
        out.campaign = {margin, bottom - px(48), first, px(48)};
        out.practice = {margin + first + gap, out.campaign.y, inner - first - gap, px(48)};
    } else {
        out.practice = {margin, bottom - px(48), inner, px(48)};
        out.campaign = {margin, out.practice.y - px(8) - px(48), inner, px(48)};
    }
    bottom = out.campaign.y - gap;
    out.intro = {margin, bottom - px(40), inner, px(40)};
    const int header_height = out.intro.y - gap - margin;
    const int heading_height = std::min(header_height, px(64));
    out.heading = {margin, margin + (header_height - heading_height) / 2, inner, heading_height};
    out.show_logo = header_height >= px(180) && inner >= px(400);
    if (out.show_logo) {
        const int logo_height = std::min(header_height, px(220));
        const int logo_width = std::min(inner, logo_height * 400 / 288);
        const int fitted_height = logo_width * 288 / 400;
        out.logo = {(width - logo_width) / 2, margin + (header_height - fitted_height) / 2,
                    logo_width, fitted_height};
    }
    const auto fits = [&](Rect r) { return r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 &&
        r.x + r.width <= width && r.y + r.height <= height; };
    out.valid = fits(out.heading) && fits(out.intro) && fits(out.campaign) && fits(out.practice) &&
        fits(out.status) && fits(out.progress) && fits(out.browse) && fits(out.version) &&
        (!out.show_logo || fits(out.logo));
    return out;
}
}
