// No start/update call occurs in these tests: no window or UI thread is opened.
#include "../src/drive_hud.cpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <cstdlib>
#include <new>

namespace fault { bool next_allocation = false; }
void* operator new(std::size_t bytes) {
    if (fault::next_allocation) { fault::next_allocation = false; throw std::bad_alloc(); }
    if (void* pointer = std::malloc(bytes ? bytes : 1)) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

namespace test {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
#define CHECK(x) check((x), #x)
rxdrive::View view() { rxdrive::View v; v.available = true; return v; }
void real_charge_ready_and_active_time() {
    auto v = view(); v.charge = 360;
    auto model = rxhud::make_model(v, true); CHECK(model.mode == rxhud::Mode::Charging);
    CHECK(std::abs(model.fraction - 0.36F) < 0.0001F && std::strcmp(model.status.data(), "36%") == 0);
    v.charge = v.capacity; v.ready = true; model = rxhud::make_model(v, true);
    CHECK(model.mode == rxhud::Mode::Ready && model.fraction == 1 && std::strcmp(model.status.data(), "READY") == 0);
    v.phase = rxdrive::Phase::ActivationQueued; model = rxhud::make_model(v, true);
    CHECK(model.mode == rxhud::Mode::Ready && model.fraction == 1 && std::strcmp(model.status.data(), "QUEUED") == 0);
    v.phase = rxdrive::Phase::Active; v.remaining_ticks = 421; model = rxhud::make_model(v, true);
    CHECK(model.mode == rxhud::Mode::Active && model.seconds == 8 && std::strcmp(model.status.data(), "8s") == 0);
    CHECK(std::abs(model.fraction - 421.0F / 900.0F) < 0.0001F);
    v.phase = rxdrive::Phase::RestorationQueued; v.remaining_ticks = 0; model = rxhud::make_model(v, true);
    CHECK(model.mode == rxhud::Mode::Active && model.fraction == 0 && model.seconds == 0 && std::strcmp(model.status.data(), "ENDING") == 0);
    v.remaining_ticks = 400; model = rxhud::make_model(v, true); CHECK(std::strcmp(model.status.data(), "ENDING") == 0 && model.fraction > 0);
}
void visibility_and_invalid_views_fail_closed() {
    auto v = view(); CHECK(rxhud::make_model(v, false).mode == rxhud::Mode::Hidden);
    v.available = false; CHECK(rxhud::make_model(v, true).mode == rxhud::Mode::Hidden); v.available = true;
    v.phase = rxdrive::Phase::Faulted; CHECK(rxhud::make_model(v, true).mode == rxhud::Mode::Hidden);
    v.phase = rxdrive::Phase::Normal; v.capacity = 0; CHECK(rxhud::make_model(v, true).mode == rxhud::Mode::Hidden);
    v.phase = rxdrive::Phase::Active; v.duration_ticks = 0; CHECK(rxhud::make_model(v, true).mode == rxhud::Mode::Hidden);
    const auto model = rxhud::make_model(view(), true);
    CHECK(rxhud::should_show(model, true, true, false, true, true));
    CHECK(!rxhud::should_show(model, false, true, false, true, true));
    CHECK(!rxhud::should_show(model, true, false, false, true, true));
    CHECK(!rxhud::should_show(model, true, true, true, true, true));
    CHECK(!rxhud::should_show(model, true, true, false, false, true));
    CHECK(!rxhud::should_show(model, true, true, false, true, false));
}
void charging_readout_reports_actual_progress_without_rounding_to_ready() {
    auto v = view();
    for (const auto& sample : {std::pair{0u,"0%"}, {1u,"0%"}, {360u,"36%"}, {999u,"99%"}, {1000u,"100%"}}) {
        v.charge = sample.first;
        const auto model = rxhud::make_model(v,true);
        CHECK(model.mode == rxhud::Mode::Charging);
        CHECK(std::strcmp(model.status.data(),sample.second) == 0);
    }
    v.capacity = 3; v.charge = 2;
    CHECK(std::strcmp(rxhud::make_model(v,true).status.data(),"66%") == 0);
    v.capacity = std::numeric_limits<unsigned>::max(); v.charge = v.capacity - 1;
    CHECK(std::strcmp(rxhud::make_model(v,true).status.data(),"99%") == 0);
    v.charge = v.capacity; v.ready = true;
    CHECK(std::strcmp(rxhud::make_model(v,true).status.data(),"READY") == 0);
}
void input_ranges_are_clamped_without_overflow() {
    auto v = view(); v.charge = std::numeric_limits<unsigned>::max();
    auto model = rxhud::make_model(v, true); CHECK(model.fraction == 1 && model.mode == rxhud::Mode::Charging);
    v.phase = rxdrive::Phase::Active; v.duration_ticks = std::numeric_limits<unsigned>::max();
    v.remaining_ticks = std::numeric_limits<unsigned>::max(); model = rxhud::make_model(v, true);
    CHECK(model.fraction == 1 && model.seconds == 71582789u && model.status.back() == 0);
    v.duration_ticks = 60; model = rxhud::make_model(v, true); CHECK(model.seconds == 1 && model.fraction == 1);
}
void layouts_fit_resizes_and_dpi_without_covering_bottom_edge() {
    CHECK(!rxhud::make_layout(0, 0).valid); CHECK(!rxhud::make_layout(279, 180).valid);
    for (const auto size : {std::pair{280,180}, {640,480}, {1280,720}, {1920,1080}, {3840,2160}, {900,1600}})
        for (unsigned dpi : {72u, 96u, 144u, 192u, 384u, std::numeric_limits<unsigned>::max()}) {
            const auto l = rxhud::make_layout(size.first, size.second, dpi); CHECK(l.valid);
            CHECK(l.panel.x >= 0 && l.panel.y >= 0);
            CHECK(l.panel.x + l.panel.width < size.first && l.panel.y + l.panel.height < size.second);
            CHECK(l.track.x > 0 && l.track.y > 0 && l.track.x + l.track.width < l.panel.width);
            CHECK(l.track.y + l.track.height < l.panel.height && l.font_px > 0);
            CHECK(size.second - (l.panel.y + l.panel.height) >= 50);
        }
    const auto normal = rxhud::make_layout(1920,1080,96), large = rxhud::make_layout(1920,1080,192);
    CHECK(large.panel.width > normal.panel.width && large.font_px > normal.font_px);
}
void raster_has_premultiplied_alpha_and_distinct_actual_fill() {
    auto v = view(); const auto layout = rxhud::make_layout(1280,720,96);
    const auto empty = rxhud::render(rxhud::make_model(v,true),layout);
    CHECK(empty.width == layout.panel.width && empty.height == layout.panel.height);
    CHECK(empty.pixels.size() == static_cast<std::size_t>(empty.width) * empty.height && empty.pixels[0] == 0);
    v.charge = 500; const auto half = rxhud::render(rxhud::make_model(v,true),layout);
    const auto at = [&](const rxhud::Image& image,int x) { return image.pixels[static_cast<std::size_t>(layout.track.y+3)*image.width+layout.track.x+x]; };
    CHECK(at(empty,10) != at(half,10)); CHECK(at(empty,layout.track.width-10) == at(half,layout.track.width-10));
    v.charge = 1000; v.ready = true; const auto ready = rxhud::render(rxhud::make_model(v,true),layout);
    const auto accent = at(ready,layout.track.width/2); CHECK(((accent>>16)&255u) > (accent&255u));
    for (const auto pixel : ready.pixels) {
        const unsigned alpha = pixel>>24;
        CHECK(((pixel>>16)&255u)<=alpha && ((pixel>>8)&255u)<=alpha && (pixel&255u)<=alpha);
    }
    CHECK(rxhud::render({},layout).pixels.empty());
    auto invalid = layout; invalid.panel.width = 100000; CHECK(rxhud::render(rxhud::make_model(v,true),invalid).pixels.empty());
}
std::array<rxhud::Model,9> display_states() {
    std::array<rxhud::Model,9> result{};
    auto v=view();
    for(unsigned index=0;index<result.size();++index) {
        v.phase=index==4?rxdrive::Phase::ActivationQueued:index==7?rxdrive::Phase::RestorationQueued:
                index>=5?rxdrive::Phase::Active:rxdrive::Phase::Normal;
        v.charge=index==0?0u:index==1?360u:index==2?990u:1000u;
        v.ready=index==3;v.remaining_ticks=index==6?120u:index==7?0u:421u;
        v.duration_ticks=index==8?std::numeric_limits<unsigned>::max():900u;
        if(index==8)v.remaining_ticks=v.duration_ticks;
        result[index]=rxhud::make_model(v,true);
    }
    return result;
}
void portable_glyphs_are_visible_crisp_and_contained() {
    constexpr std::uint32_t foreground=0xffe2f0f6u, shadow=0xff000308u;
    for(int unit:{1,2,3,4,8})for(const char* word:{"DRIVE","READY","QUEUED","ENDING","100%","71582789s"}) {
        rxhud::Image image;image.width=64*unit;image.height=14*unit;
        image.pixels.resize(static_cast<std::size_t>(image.width)*image.height);
        const rxhud::Rect box{4*unit,3*unit,54*unit,8*unit};
        CHECK(rxhud::text(image,box,7*unit,word,true,{226,240,246,255}));
        unsigned ink=0,shade=0;
        for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
            const auto pixel=image.pixels[static_cast<std::size_t>(y)*image.width+x];
            if(pixel)CHECK(x>=box.x&&x<box.x+box.width&&y>=box.y&&y<box.y+box.height);
            CHECK(pixel==0||pixel==foreground||pixel==shadow);
            ink+=pixel==foreground;shade+=pixel==shadow;
            // Every logical pixel remains a solid square, without interpolation.
            CHECK(pixel==image.pixels[static_cast<std::size_t>(y/unit*unit)*image.width+x/unit*unit]);
        }
        CHECK(ink>=40u*unit*unit&&shade>=10u*unit*unit);
    }
    const auto layout=rxhud::make_layout(1280,720);
    const auto image=rxhud::render(display_states()[0],layout);
    const int unit=layout.font_px/7;
    const auto at=[&](int x,int y){return image.pixels[static_cast<std::size_t>(layout.label.y+y*unit)*image.width+layout.label.x+x*unit];};
    // The first D has a four-pixel top, hollow body and its right stem below it.
    CHECK(at(0,0)==foreground&&at(3,0)==foreground&&at(4,0)!=foreground);
    CHECK(at(0,1)==foreground&&at(1,1)!=foreground&&at(4,1)==foreground);
    CHECK(layout.panel.width*layout.panel.height<224*58); // more room for battle visibility
}
void every_state_fits_minimum_windows_dpi_and_long_timers() {
    const auto states=display_states();
    for(const auto& size:{std::pair{280,180},{320,240},{640,480},{720,360},{1280,720},{1920,1080},{3840,2160},{900,1600}})
    for(unsigned dpi:{72u,96u,144u,192u,384u,std::numeric_limits<unsigned>::max()}) {
        const auto layout=rxhud::make_layout(size.first,size.second,dpi);CHECK(layout.valid);
        for(const auto& state:states) {
            const auto image=rxhud::render(state,layout);CHECK(!image.pixels.empty());
            unsigned label_ink=0,status_ink=0;
            const auto white=0xffe2f0f6u;
            const auto status_color=state.mode==rxhud::Mode::Ready?0xffffcd53u:
                state.seconds>0&&state.seconds<=3?0xffffa346u:white;
            for(int y=layout.label.y;y<layout.label.y+layout.label.height;++y)
                for(int x=layout.label.x;x<layout.label.x+layout.label.width;++x)
                    label_ink+=image.pixels[static_cast<std::size_t>(y)*image.width+x]==white;
            for(int y=layout.status.y;y<layout.status.y+layout.status.height;++y)
                for(int x=layout.status.x;x<layout.status.x+layout.status.width;++x)
                    status_ink+=image.pixels[static_cast<std::size_t>(y)*image.width+x]==status_color;
            CHECK(label_ink>0&&status_ink>0);
            for(auto pixel:image.pixels) {
                const unsigned alpha=pixel>>24;
                CHECK(alpha==0||alpha==180||alpha==255); // no antialiased edge coverage
                CHECK(((pixel>>16)&255u)<=alpha&&((pixel>>8)&255u)<=alpha&&(pixel&255u)<=alpha);
            }
        }
    }
    auto layout=rxhud::make_layout(1280,720);auto model=states[0];model.status.fill('X');
    CHECK(rxhud::render(model,layout).pixels.empty()); // bounded malformed status, no strlen overrun
    layout.status.x=std::numeric_limits<int>::max();
    CHECK(rxhud::render(states[0],layout).pixels.empty());
}
void low_time_uses_a_steady_warm_cue_without_changing_timer() {
    auto v=view();v.phase=rxdrive::Phase::Active;v.duration_ticks=900;
    const auto layout=rxhud::make_layout(1280,720);
    const auto accent=[&](const rxhud::Image& image){return image.pixels[static_cast<std::size_t>(layout.track.y+2*(layout.font_px/7))*image.width+layout.track.x+3*(layout.font_px/7)];};
    v.remaining_ticks=181;const auto before=rxhud::render(rxhud::make_model(v,true),layout);
    CHECK((accent(before)&255u)>((accent(before)>>16)&255u));
    v.remaining_ticks=180;const auto model=rxhud::make_model(v,true);CHECK(model.seconds==3);
    const auto low=rxhud::render(model,layout);
    CHECK(((accent(low)>>16)&255u)>(accent(low)&255u));
    CHECK(low.pixels==rxhud::render(model,layout).pixels); // static cue, no flashing
    CHECK(std::strcmp(model.status.data(),"3s")==0);
}
void repeated_offscreen_rendering_releases_gdi_resources() {
#ifdef _WIN32
    auto v = view(); v.charge=1000;v.ready=true;
    const auto layout=rxhud::make_layout(1280,720,144);
    CHECK(!rxhud::render(rxhud::make_model(v,true),layout).pixels.empty()); // offscreen render must not create GDI font resources
    const DWORD before_gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    const DWORD before_user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(unsigned i=0;i<50;++i) {
        v.phase=i%2?rxdrive::Phase::Active:rxdrive::Phase::ActivationQueued;v.remaining_ticks=900-i;
        CHECK(!rxhud::render(rxhud::make_model(v,true),layout).pixels.empty());
    }
    CHECK(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==before_gdi);
    CHECK(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==before_user);
#endif
}
void allocation_failure_stays_inside_hud_and_later_render_recovers() {
    static_assert(noexcept(rxhud::start()) && noexcept(rxhud::stop()));
    static_assert(noexcept(rxhud::update(rxdrive::View{},false)) && noexcept(rxhud::set_enabled(false)));
    auto v=view();v.charge=500;const auto layout=rxhud::make_layout(1280,720);
    const auto model=rxhud::make_model(v,true);
    fault::next_allocation=true;
    const auto failed=rxhud::render(model,layout);
    CHECK(!fault::next_allocation && failed.pixels.empty());
    CHECK(!rxhud::render(model,layout).pixels.empty());
#ifdef _WIN32
    // start's FIRST allocation is Shared, before any events, worker or window.
    // Failing it exercises the actual native-facing exception fence without
    // launching a UI thread or creating even an invisible helper window.
    const DWORD before_user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    fault::next_allocation=true;
    CHECK(!rxhud::start());CHECK(!fault::next_allocation);
    CHECK(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==before_user);
#endif
    rxhud::update(v,false);rxhud::set_enabled(false);rxhud::stop(); // no host exists
}
#ifdef _WIN32
namespace identity {
struct State {
    bool valid=true, exists=true, responsive=true, title_ok=true, timeout_set=false;
    HICON small=reinterpret_cast<HICON>(1), large=reinterpret_cast<HICON>(2);
    std::array<wchar_t,256> caption{};
    unsigned messages=0, icon_sets=0, title_sets=0, destroyed=0;
} state;
HWND window() {return reinterpret_cast<HWND>(1);}
void caption(const wchar_t* text) {
    state.caption.fill(0);
    std::wmemcpy(state.caption.data(),text,std::min(std::wcslen(text),state.caption.size()-1));
}
void reset() {state={};caption(L"Kingdom Hearts 358/2 Days (native)");}
bool valid(HWND candidate) {return state.valid && candidate==window();}
bool exists(HWND candidate) {return state.exists && candidate==window();}
int read(HWND,wchar_t* output,int size) {
    const auto length=std::min(static_cast<int>(std::wcslen(state.caption.data())),size-1);
    std::wmemcpy(output,state.caption.data(),length);output[length]=0;return length;
}
HICON icon(HMODULE,int size) {
    return reinterpret_cast<HICON>(static_cast<INT_PTR>(size==GetSystemMetrics(SM_CXSMICON)?101:102));
}
void destroy(HICON value) {if(value)++state.destroyed;}
bool message(HWND,UINT kind,WPARAM size,LPARAM data,DWORD_PTR& result) {
    ++state.messages;
    if(!state.responsive)return false;
    HICON& current=size==ICON_SMALL?state.small:state.large;
    result=reinterpret_cast<DWORD_PTR>(current);
    if(kind==WM_SETICON){++state.icon_sets;current=reinterpret_cast<HICON>(data);if(state.timeout_set)return false;}
    return true;
}
bool title(HWND,const wchar_t* text) {++state.title_sets;if(!state.title_ok)return false;caption(text);return true;}
rxhud::IdentityApi api() {return {&valid,&exists,&read,&icon,&destroy,&message,&title};}
}
void game_identity_is_scoped_to_this_process_and_native_main_window() {
    const DWORD pid=GetCurrentProcessId();
    CHECK(rxhud::identity_candidate(pid,false,0,0,L"SDL_app"));
    CHECK(!rxhud::identity_candidate(pid+1,false,0,0,L"SDL_app"));
    CHECK(!rxhud::identity_candidate(pid,true,0,0,L"SDL_app"));
    CHECK(!rxhud::identity_candidate(pid,false,WS_CHILD,0,L"SDL_app"));
    CHECK(!rxhud::identity_candidate(pid,false,0,WS_EX_TOOLWINDOW,L"SDL_app"));
    CHECK(!rxhud::identity_candidate(pid,false,0,0,L"OtherGame"));
    identity::reset();rxhud::WindowIdentity owned(identity::api());
    owned.attach(reinterpret_cast<HWND>(2),nullptr);
    CHECK(identity::state.messages==0 && identity::state.title_sets==0);
}
void game_identity_restores_its_icons_and_caption_once() {
    identity::reset();rxhud::WindowIdentity owned(identity::api());
    owned.attach(identity::window(),nullptr);
    CHECK(identity::state.small==reinterpret_cast<HICON>(101) && identity::state.large==reinterpret_cast<HICON>(102));
    CHECK(std::wcscmp(identity::state.caption.data(),rxhud::kGameTitle)==0);
    const unsigned calls=identity::state.messages;
    owned.attach(identity::window(),nullptr);CHECK(identity::state.messages==calls);
    owned.reset();
    CHECK(identity::state.small==reinterpret_cast<HICON>(1) && identity::state.large==reinterpret_cast<HICON>(2));
    CHECK(std::wcscmp(identity::state.caption.data(),L"Kingdom Hearts 358/2 Days (native)")==0);
    CHECK(identity::state.icon_sets==4 && identity::state.title_sets==2 && identity::state.destroyed==2);
}
void game_identity_preserves_newer_changes_and_tolerates_unresponsive_or_retired_windows() {
    identity::reset();rxhud::WindowIdentity owned(identity::api());owned.attach(identity::window(),nullptr);
    identity::state.small=reinterpret_cast<HICON>(301);identity::state.large=reinterpret_cast<HICON>(302);
    identity::caption(L"A newer caption");owned.reset();
    CHECK(identity::state.icon_sets==2 && identity::state.title_sets==1);
    CHECK(identity::state.small==reinterpret_cast<HICON>(301) && identity::state.large==reinterpret_cast<HICON>(302));
    CHECK(std::wcscmp(identity::state.caption.data(),L"A newer caption")==0);
    CHECK(identity::state.destroyed==2);
    identity::reset();identity::state.responsive=false;identity::state.title_ok=false;
    owned.attach(identity::window(),nullptr);const unsigned failed=identity::state.messages;
    owned.attach(identity::window(),nullptr);CHECK(identity::state.messages==failed && identity::state.icon_sets==0);
    identity::state.valid=false;identity::state.exists=false;owned.reset();CHECK(identity::state.messages==failed);
    CHECK(identity::state.destroyed==2);
}
void game_identity_keeps_possibly_in_use_icons_valid_after_a_timeout() {
    identity::reset();identity::state.timeout_set=true;
    rxhud::WindowIdentity owned(identity::api());owned.attach(identity::window(),nullptr);
    CHECK(identity::state.small==reinterpret_cast<HICON>(101));
    owned.reset();CHECK(identity::state.destroyed==0);
    // A destroyed window no longer uses either independent icon copy.
    identity::reset();identity::state.timeout_set=true;owned.attach(identity::window(),nullptr);
    identity::state.valid=false;identity::state.exists=false;owned.reset();
    CHECK(identity::state.destroyed==2);
}
#endif
void write_preview(const char* path) {
    constexpr int width=340,height=540;
    std::vector<std::uint32_t> pixels(width*height,0xff142234u);
    auto v=view(); const auto layout=rxhud::make_layout(1280,720,96);
    for (int row=0;row<8;++row) {
        v.phase=row==4?rxdrive::Phase::ActivationQueued:row==7?rxdrive::Phase::RestorationQueued:
                row>=5?rxdrive::Phase::Active:rxdrive::Phase::Normal;
        v.charge=row==0?0u:row==1?360u:row==2?990u:1000u;v.ready=row==3;
        v.remaining_ticks=row==6?120u:row==7?0u:421u;
        const auto image=rxhud::render(rxhud::make_model(v,true),layout);
        CHECK(!image.pixels.empty());
        for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
            const auto source=image.pixels[static_cast<std::size_t>(y)*image.width+x];
            auto& dest=pixels[static_cast<std::size_t>(18+row*65+y)*width+(width-image.width)/2+x];
            const unsigned a=source>>24,inverse=255-a;
            const auto add=[&](unsigned shift) {return ((source>>shift)&255u)+((((dest>>shift)&255u)*inverse+127)/255);};
            dest=0xff000000u|(add(16)<<16)|(add(8)<<8)|add(0);
        }
    }
    std::ofstream file(path,std::ios::binary); CHECK(file.good());
    const auto word=[&](std::uint32_t n,int bytes) {for(int i=0;i<bytes;++i)file.put(static_cast<char>((n>>(i*8))&255u));};
    file.write("BM",2);word(54+width*height*4,4);word(0,4);word(54,4);
    word(40,4);word(width,4);word(static_cast<std::uint32_t>(-height),4);word(1,2);word(32,2);
    word(0,4);word(width*height*4,4);word(3780,4);word(3780,4);word(0,4);word(0,4);
    file.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()*sizeof(std::uint32_t)));
    CHECK(file.good());
}
}
int main(int argc,char** argv) {
    const std::pair<const char*,void(*)()> cases[]={
        {"actual charge, ready and timed Drive view",test::real_charge_ready_and_active_time},
        {"visibility, focus, minimized and stale gates",test::visibility_and_invalid_views_fail_closed},
        {"charging percentage is exact and never claims readiness early",test::charging_readout_reports_actual_progress_without_rounding_to_ready},
        {"unsigned resource/time ranges are bounded",test::input_ranges_are_clamped_without_overflow},
        {"client resize and DPI layout bounds",test::layouts_fit_resizes_and_dpi_without_covering_bottom_edge},
        {"premultiplied raster and real fraction fill",test::raster_has_premultiplied_alpha_and_distinct_actual_fill},
        {"portable pixel glyphs are visible, crisp and contained",test::portable_glyphs_are_visible_crisp_and_contained},
        {"all HUD states fit small windows, high DPI and long timers",test::every_state_fits_minimum_windows_dpi_and_long_timers},
        {"last three seconds use a steady warm cue",test::low_time_uses_a_steady_warm_cue_without_changing_timer},
#ifdef _WIN32
        {"offscreen GDI and USER resources are released",test::repeated_offscreen_rendering_releases_gdi_resources},
#endif
        {"allocation failures are contained and raster recovers",test::allocation_failure_stays_inside_hud_and_later_render_recovers},
#ifdef _WIN32
        {"game identity refuses other processes, owned tools and non-native windows",test::game_identity_is_scoped_to_this_process_and_native_main_window},
        {"game identity applies once and restores its prior caption and icons",test::game_identity_restores_its_icons_and_caption_once},
        {"game identity preserves newer changes and bounds failed or retired windows",test::game_identity_preserves_newer_changes_and_tolerates_unresponsive_or_retired_windows},
        {"timed-out icon writes remain valid through DLL unload",test::game_identity_keeps_possibly_in_use_icons_valid_after_a_timeout},
#endif
    };
    unsigned failures=0;
    for(const auto& entry:cases)try{entry.second();std::cout<<"PASS "<<entry.first<<'\n';}
        catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<entry.first<<": "<<e.what()<<'\n';}
    if(argc>1)try{test::write_preview(argv[1]);std::cout<<"Wrote offline preview; no window opened\n";}
        catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<std::size(cases)-failures<<'/'<<std::size(cases)<<" HUD tests passed\n";
    return failures?1:0;
}
