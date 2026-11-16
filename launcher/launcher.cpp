#include "setup.h"
#include "version.h"
#include "layout.h"
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
    HWND logo_control = nullptr, heading_control = nullptr, intro = nullptr, version = nullptr, restore_focus = nullptr;
    HFONT regular = nullptr, heading = nullptr;
    HBITMAP logo = nullptr;
    int font_pixels = 0, logo_width = 0, logo_height = 0, default_id = 10;
    HBRUSH background = CreateSolidBrush(RGB(247, 248, 250));
    Options options;
    fs::path own_folder;
    std::thread worker;
    bool busy = false;
    bool compact_title = false;
    ~App() { if (worker.joinable()) worker.join(); if (regular) DeleteObject(regular); if (heading) DeleteObject(heading); if (logo) DeleteObject(logo); DeleteObject(background); }
    void focus_action() {
        if (busy || GetForegroundWindow() != window) return;
        const HWND current=GetFocus();
        if(current && current!=window && IsChild(window,current) && IsWindowEnabled(current))return;
        const HWND target = restore_focus && IsWindow(restore_focus) && IsWindowEnabled(restore_focus) ? restore_focus : campaign;
        if (target) SetFocus(target);
    }
    void layout() {
        if(!campaign)return;
        RECT client{}; if (!GetClientRect(window, &client)) return;
        const auto positions = ui::calculate(client.right, client.bottom, GetDpiForWindow(window));
        if (!positions.valid) return;
        const int pixels = std::max(10, static_cast<int>(std::lround(44.0F / 3 * positions.scale)));
        const bool short_title=positions.heading.height < static_cast<int>(std::lround(56 * positions.scale));
        if(short_title!=compact_title) {
            SetWindowTextW(heading_control,short_title?L"358/2 Days: Pragmicat Mix":L"Kingdom Hearts 358/2 Days\nPragmicat Mix");
            compact_title=short_title;
        }
        if (pixels != font_pixels) {
            HFONT next = CreateFontW(-pixels,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            HFONT next_heading = CreateFontW(-std::max(14, static_cast<int>(std::lround(80.0F / 3 * positions.scale))),0,0,0,
                FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            if (next && next_heading) {
                for (HWND control : {campaign,practice,browse,status,intro,version}) SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
                SendMessageW(heading_control,WM_SETFONT,reinterpret_cast<WPARAM>(next_heading),TRUE);
                if (regular) DeleteObject(regular); if (heading) DeleteObject(heading);
                regular=next; heading=next_heading; font_pixels=pixels;
            } else { if(next)DeleteObject(next); if(next_heading)DeleteObject(next_heading); }
        }
        const auto move = [](HWND control,ui::Rect area) { MoveWindow(control,area.x,area.y,area.width,area.height,TRUE); };
        move(heading_control,positions.heading); move(intro,positions.intro);
        move(campaign,positions.campaign); move(practice,positions.practice);
        move(status,positions.status); move(progress,positions.progress);
        move(browse,positions.browse); move(version,positions.version);
        if (positions.show_logo) {
            move(logo_control,positions.logo);
            if (logo_width!=positions.logo.width || logo_height!=positions.logo.height) {
                HBITMAP next=logo_bitmap(positions.logo.width,positions.logo.height);
                SendMessageW(logo_control,STM_SETIMAGE,IMAGE_BITMAP,reinterpret_cast<LPARAM>(next));
                if(logo)DeleteObject(logo); logo=next;
                logo_width=positions.logo.width; logo_height=positions.logo.height;
            }
        } else {
            SendMessageW(logo_control,STM_SETIMAGE,IMAGE_BITMAP,0);
            if(logo)DeleteObject(logo); logo=nullptr; logo_width=logo_height=0;
        }
        ShowWindow(logo_control,positions.show_logo&&logo?SW_SHOW:SW_HIDE);
        ShowWindow(heading_control,positions.show_logo&&logo?SW_HIDE:SW_SHOW);
        ShowWindow(progress,busy?SW_SHOW:SW_HIDE);
    }
    void set_busy(bool value) {
        if (value) {
            const HWND focused=GetFocus();
            if(focused==campaign || focused==practice || focused==browse)restore_focus=focused;
        }
        busy = value;
        for (const auto control : {campaign, practice, browse}) EnableWindow(control, !value);
        SendMessageW(progress, PBM_SETMARQUEE, value ? TRUE : FALSE, 30);
        ShowWindow(progress, value ? SW_SHOW : SW_HIDE);
        if(!value)focus_action();
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
        const auto make = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id) {
            return CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        };
        app->logo_control=make(L"STATIC",L"",SS_BITMAP|SS_CENTERIMAGE,0);
        app->heading_control=make(L"STATIC",L"Kingdom Hearts 358/2 Days\nPragmicat Mix",SS_CENTER,0);
        app->intro=make(L"STATIC",L"Campaign and combat practice.\nOne-time setup imports your Days game; progress stays separate.",SS_LEFT,0);
        app->campaign=make(L"BUTTON",L"Play Campaign",BS_DEFPUSHBUTTON|WS_TABSTOP,10);
        app->practice=make(L"BUTTON",L"Combat Practice",BS_PUSHBUTTON|WS_TABSTOP,11);
        app->status=make(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,0);
        app->progress=make(PROGRESS_CLASSW,L"",PBS_MARQUEE,0);
        app->browse=make(L"BUTTON",L"Choose Days game...",BS_PUSHBUTTON|WS_TABSTOP,12);
        const auto label=L"Pragmicat Mix "+wide(PRAGMICAT_VERSION_STRING)+L"  |  Days port 0.1.5";
        app->version=make(L"STATIC",label.c_str(),SS_RIGHT,0);
        app->layout(); app->start(false,true); return 0;
    }
    case WM_SIZE: app->layout(); return 0;
    case WM_SETFOCUS: app->focus_action(); return 0;
    case WM_DPICHANGED: {
        const auto* suggested=reinterpret_cast<const RECT*>(lparam);
        RECT fitted=*suggested; MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromRect(suggested,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const int width=std::min(fitted.right-fitted.left,monitor.rcWork.right-monitor.rcWork.left);
            const int height=std::min(fitted.bottom-fitted.top,monitor.rcWork.bottom-monitor.rcWork.top);
            fitted.left=std::clamp(fitted.left,monitor.rcWork.left,monitor.rcWork.right-width);
            fitted.top=std::clamp(fitted.top,monitor.rcWork.top,monitor.rcWork.bottom-height);
            fitted.right=fitted.left+width; fitted.bottom=fitted.top+height;
        }
        SetWindowPos(window,nullptr,fitted.left,fitted.top,fitted.right-fitted.left,
            fitted.bottom-fitted.top,SWP_NOZORDER|SWP_NOACTIVATE);
        app->layout(); return 0;
    }
    case WM_GETMINMAXINFO: {
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const unsigned dpi=GetDpiForWindow(window); RECT chrome{};
            AdjustWindowRectExForDpi(&chrome,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,0,dpi);
            const int chrome_width=chrome.right-chrome.left,chrome_height=chrome.bottom-chrome.top;
            const auto size=ui::fit_client(monitor.rcWork.right-monitor.rcWork.left,
                monitor.rcWork.bottom-monitor.rcWork.top,dpi,chrome_width,chrome_height,true);
            auto* bounds=reinterpret_cast<MINMAXINFO*>(lparam);
            bounds->ptMinTrackSize={size.width+chrome_width,size.height+chrome_height};
        }
        return 0;
    }
    case DM_GETDEFID: return MAKELRESULT(app->default_id,DC_HASDEFID);
    case DM_SETDEFID: {
        const int id=static_cast<int>(wparam);
        if(id==10 || id==11 || id==12) {
            SendMessageW(GetDlgItem(window,app->default_id),BM_SETSTYLE,BS_PUSHBUTTON,TRUE);
            app->default_id=id;
            SendMessageW(GetDlgItem(window,id),BM_SETSTYLE,BS_DEFPUSHBUTTON,TRUE);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            const int command=ui::resolve_command(LOWORD(wparam),app->default_id,app->busy);
            if (command==IDCANCEL) { SendMessageW(window,WM_CLOSE,0,0); return 0; }
            if (command == 10) app->start(false); else if (command == 11) app->start(true);
            else if (command == 12) try { app->choose_game(); } catch (const std::exception& error) { MessageBoxW(window, wide(error.what()).c_str(), title, MB_OK | MB_ICONERROR); }
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
    const UINT dpi=GetDpiForSystem();
    constexpr DWORD style=WS_OVERLAPPEDWINDOW;
    RECT chrome{}; AdjustWindowRectExForDpi(&chrome,style,FALSE,0,dpi);
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&monitor))
        monitor.rcWork={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    const auto size=ui::fit_client(monitor.rcWork.right-monitor.rcWork.left,
        monitor.rcWork.bottom-monitor.rcWork.top,dpi,chrome.right-chrome.left,chrome.bottom-chrome.top);
    RECT area{0,0,size.width,size.height}; AdjustWindowRectExForDpi(&area,style,FALSE,0,dpi);
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
