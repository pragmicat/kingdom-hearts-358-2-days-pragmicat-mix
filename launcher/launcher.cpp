#include "setup.h"
#include "version.h"
#include <commctrl.h>
#include <ole2.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <array>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace {
using namespace zw;
constexpr UINT progress_message = WM_APP + 1, completed_message = WM_APP + 2;
struct GraphicsSession {
    ULONG_PTR token = 0;
    GraphicsSession() { Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&token, &input, nullptr); }
    ~GraphicsSession() { if (token) Gdiplus::GdiplusShutdown(token); }
};
HBITMAP logo_bitmap(int width, int height) {
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(209), RT_RCDATA);
    const DWORD size = resource ? SizeofResource(module, resource) : 0;
    const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!size || !bytes) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    void* destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return nullptr; }
    std::memcpy(destination, bytes, size); GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) { GlobalFree(memory); return nullptr; }
    HBITMAP result = nullptr;
    {
        Gdiplus::Bitmap image(stream, FALSE);
        if (image.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Bitmap scaled(width, height, PixelFormat24bppRGB);
            {
                Gdiplus::Graphics graphics(&scaled);
                graphics.Clear(Gdiplus::Color(255, 247, 248, 250));
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                graphics.DrawImage(&image, 0, 0, width, height);
            }
            scaled.GetHBITMAP(Gdiplus::Color(255, 247, 248, 250), &result);
        }
    }
    stream->Release();
    return result;
}
struct Completion { std::wstring message; fs::path game; bool error = false, discovery = false; };
struct App {
    HWND window = nullptr, campaign = nullptr, practice = nullptr, browse = nullptr, status = nullptr, progress = nullptr;
    HFONT regular = nullptr, heading = nullptr;
    HBITMAP logo = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(247, 248, 250));
    Options options;
    fs::path own_folder;
    std::thread worker;
    bool busy = false;
    ~App() { if (worker.joinable()) worker.join(); if (regular) DeleteObject(regular); if (heading) DeleteObject(heading); if (logo) DeleteObject(logo); DeleteObject(background); }
    void set_busy(bool value) {
        busy = value;
        for (const auto control : {campaign, practice, browse}) EnableWindow(control, !value);
        SendMessageW(progress, PBM_SETMARQUEE, value ? TRUE : FALSE, 30);
        ShowWindow(progress, value ? SW_SHOW : SW_HIDE);
    }
    void send_progress(const std::wstring& text) {
        auto message = std::make_unique<std::wstring>(text);
        if (PostMessageW(window, progress_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    }
    void start(bool practice_mode, bool discovery = false) {
        if (busy) return;
        if (worker.joinable()) worker.join();
        if (!discovery && !installed(options.root) && options.game.empty()) {
            try { choose_game(); } catch (const std::exception& error) { MessageBoxW(window, wide(error.what()).c_str(), title, MB_OK | MB_ICONINFORMATION); }
            if (options.game.empty()) return;
        }
        set_busy(true);
        SetWindowTextW(status, discovery ? L"Looking for your installed Days PC port..." : L"Preparing Pragmicat Mix...");
        worker = std::thread([this, practice_mode, discovery] {
            auto completed = std::make_unique<Completion>(); completed->discovery = discovery;
            try {
                fs::path game = options.game;
                if (!installed(options.root) && (game.empty() || !supported_game(game))) game = discover_game(options.root, own_folder);
                if (discovery) {
                    completed->game = game;
                    completed->message = installed(options.root) ? L"Ready. Your separate game copy and saves are here." :
                        game.empty() ? L"Choose your installed Days game once, then use either button." : L"Game found. Choose Campaign or Practice to make your separate copy.";
                } else {
                    const auto prepared = prepare(options.root, game, practice_mode, [this](const auto& text) { send_progress(text); });
                    launch_game(prepared, practice_mode); completed->game = game;
                    completed->message = practice_mode ? L"Combat Practice is running. Campaign saves stay separate." : L"Campaign is running. Progress is saved in your Pragmicat Mix copy.";
                }
            } catch (const std::exception& error) { completed->message = wide(error.what()); completed->error = true; }
            if (PostMessageW(window, completed_message, 0, reinterpret_cast<LPARAM>(completed.get()))) completed.release();
        });
    }
    void choose_game() {
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) fail(L"Could not open the game chooser.");
        COMDLG_FILTERSPEC filters[]{{L"Days native game (khdays-native.exe)", L"khdays-native.exe"}, {L"Windows programs", L"*.exe"}};
        dialog->SetFileTypes(2, filters); dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
        dialog->SetTitle(L"Choose khdays-native.exe from your installed Days PC port 0.1.5");
        if (SUCCEEDED(dialog->Show(window))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                wchar_t* selected = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) {
                    const fs::path path(selected); CoTaskMemFree(selected);
                    if (supported_game(path)) { options.game = path; SetWindowTextW(status, L"Game selected. Choose Campaign or Practice to continue."); }
                    else MessageBoxW(window, L"Choose the matching native Days PC port 0.1.5 with its extracted game data beside it. Your original game will stay unchanged.", title, MB_OK | MB_ICONINFORMATION);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
};
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); app->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app)); }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        const UINT dpi = GetDpiForWindow(window);
        const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
        app->regular = CreateFontW(-MulDiv(11, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        app->heading = CreateFontW(-MulDiv(20, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        const auto make = [&](const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id) {
            HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height), window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->regular), TRUE); return control;
        };
        app->logo = logo_bitmap(scale(400), scale(288));
        if (app->logo) {
            const auto logo = make(L"STATIC", L"Kingdom Hearts 358/2 Days: Pragmicat Mix", SS_BITMAP | SS_CENTERIMAGE, 120, 4, 400, 288, 0);
            SendMessageW(logo, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(app->logo));
        } else {
            const auto heading = make(L"STATIC", L"Kingdom Hearts 358/2 Days\nPragmicat Mix", SS_CENTER, 32, 90, 576, 90, 0);
            SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(app->heading), TRUE);
        }
        make(L"STATIC", L"Kingdom Hearts 358/2 Days: campaign and combat practice.\nOne-time setup imports your Days game; progress stays separate.", SS_LEFT, 32, 304, 576, 48, 0);
        app->campaign = make(L"BUTTON", L"Play 358/2 Days Campaign", BS_DEFPUSHBUTTON | WS_TABSTOP, 32, 370, 280, 52, 10);
        app->practice = make(L"BUTTON", L"358/2 Days Combat Practice", BS_PUSHBUTTON | WS_TABSTOP, 328, 370, 280, 52, 11);
        app->status = make(L"STATIC", L"", SS_LEFT, 32, 438, 576, 52, 0);
        app->progress = make(PROGRESS_CLASSW, L"", PBS_MARQUEE, 32, 494, 576, 5, 0);
        app->browse = make(L"BUTTON", L"Choose Days game...", BS_PUSHBUTTON | WS_TABSTOP, 32, 516, 180, 30, 12);
        const auto version_label = L"Pragmicat Mix " + wide(PRAGMICAT_VERSION_STRING) + L" · Yokimitsuro port 0.1.5";
        make(L"STATIC", version_label.c_str(), SS_RIGHT, 246, 523, 362, 24, 0);
        app->start(false, true); return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            if (LOWORD(wparam) == 10) app->start(false); else if (LOWORD(wparam) == 11) app->start(true);
            else if (LOWORD(wparam) == 12) try { app->choose_game(); } catch (const std::exception& error) { MessageBoxW(window, wide(error.what()).c_str(), title, MB_OK | MB_ICONERROR); }
        }
        return 0;
    case progress_message: { std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam)); SetWindowTextW(app->status, text->c_str()); return 0; }
    case completed_message: {
        std::unique_ptr<Completion> completed(reinterpret_cast<Completion*>(lparam)); if (app->worker.joinable()) app->worker.join(); app->set_busy(false);
        if (!completed->game.empty()) app->options.game = completed->game;
        SetWindowTextW(app->status, completed->message.c_str());
        if (completed->error) MessageBoxW(window, completed->message.c_str(), title, MB_OK | MB_ICONINFORMATION);
        if (completed->discovery && app->options.practice) { app->options.practice = false; app->start(true); }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), RGB(38, 42, 49)); SetBkColor(reinterpret_cast<HDC>(wparam), RGB(247, 248, 250)); return reinterpret_cast<LRESULT>(app->background);
    case WM_ERASEBKGND: { RECT area{}; GetClientRect(window, &area); FillRect(reinterpret_cast<HDC>(wparam), &area, app->background); return 1; }
    case WM_CLOSE:
        if (app->busy) { SetWindowTextW(app->status, L"Finishing setup. You can close this window when it completes."); return 0; }
        DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
int run_gui(Options options) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GraphicsSession graphics;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
    App app; app.options = std::move(options);
    std::vector<wchar_t> executable(32768); GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())); app.own_folder = fs::path(executable.data()).parent_path();
    WNDCLASSEXW window_class{}; window_class.cbSize = sizeof(window_class); window_class.lpfnWndProc = window_proc; window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hIcon = LoadIconW(window_class.hInstance, MAKEINTRESOURCEW(101)); window_class.hIconSm = window_class.hIcon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW); window_class.hbrBackground = app.background; window_class.lpszClassName = L"KingdomHearts358Days.PragmicatMix.Launcher";
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) fail(L"Could not open the launcher.");
    const UINT dpi = GetDpiForSystem(); RECT area{0, 0, MulDiv(640, static_cast<int>(dpi), 96), MulDiv(568, static_cast<int>(dpi), 96)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&area, style, FALSE, 0, dpi);
    const HWND window = CreateWindowExW(0, window_class.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left, area.bottom - area.top, nullptr, nullptr, window_class.hInstance, &app);
    if (!window) fail(L"Could not open the launcher window.");
    ShowWindow(window, SW_SHOW); UpdateWindow(window); MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (SUCCEEDED(initialized)) CoUninitialize(); return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
    zw::Options options;
    try {
        options = zw::parse_options();
        if (options.self_test) { zw::self_test(options.report); return 0; }
        if (options.root.empty()) options.root = zw::default_root();
        if (options.check) {
            if (!zw::installed(options.root) && options.game.empty()) zw::fail(L"--check-setup needs --game when this profile has not been prepared.");
            const auto p = zw::prepare(options.root, options.game, options.practice, [](const auto&) {});
            zw::report(options.report, "{\"ok\":true,\"mode\":" + zw::quoted(options.practice ? L"practice" : L"campaign") +
                ",\"profile_root\":" + zw::quoted(p.root.wstring()) + ",\"original_game\":" + zw::quoted(p.source.wstring()) +
                ",\"runtime\":" + zw::quoted(p.executable.wstring()) + ",\"profile\":" + zw::quoted(p.profile.wstring()) +
                ",\"campaign_profile\":" + zw::quoted((p.root / L"campaign").wstring()) + ",\"practice_profile\":" + zw::quoted((p.root / L"practice").wstring()) +
                ",\"owned_game_data\":" + zw::quoted((p.root / L"game-data").wstring()) + ",\"save_copied\":" + (p.seeded.save ? "true" : "false") +
                ",\"settings_copied\":" + (p.seeded.settings ? "true" : "false") + ",\"payload_files\":" + std::to_string(p.payload_files) +
                ",\"campaign_save_copied\":" + (p.campaign_seeded.save ? "true" : "false") + ",\"practice_save_copied\":" + (p.practice_seeded.save ? "true" : "false") +
                ",\"independent_data_copy\":true,\"original_install_modified\":false,\"game_launched\":false}");
            return 0;
        }
        return run_gui(std::move(options));
    } catch (const std::exception& error) {
        if (options.check || options.self_test) { try { zw::report(options.report, "{\"ok\":false,\"game_launched\":false,\"error\":" + zw::quoted(zw::wide(error.what())) + "}"); } catch (...) {} }
        else MessageBoxW(nullptr, zw::wide(error.what()).c_str(), zw::title, MB_OK | MB_ICONINFORMATION);
        return 1;
    }
}
