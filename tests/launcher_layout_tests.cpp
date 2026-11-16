#include "../launcher/layout.h"
#include <array>
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
#define CHECK(x) check((x), #x)
bool overlaps(zw::ui::Rect a, zw::ui::Rect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}
void bounds_and_sections() {
    for (const auto& size : {std::pair{320,300}, {480,440}, {640,568}, {960,681},
                           {1024,650}, {1280,720}, {1920,1080}, {3840,2160}, {768,1024}})
        for (unsigned dpi : {72u,96u,144u,192u,288u,384u}) {
            const auto l = zw::ui::calculate(size.first,size.second,dpi); CHECK(l.valid);
            const std::array<zw::ui::Rect,8> controls = {l.show_logo?l.logo:l.heading,l.intro,
                l.campaign,l.practice,l.status,l.progress,l.browse,l.version};
            for (std::size_t i=0;i<controls.size();++i) {
                CHECK(controls[i].x>=0 && controls[i].y>=0 && controls[i].width>0 && controls[i].height>0);
                CHECK(controls[i].x+controls[i].width<=size.first && controls[i].y+controls[i].height<=size.second);
                for(std::size_t j=i+1;j<controls.size();++j) CHECK(!overlaps(controls[i],controls[j]));
            }
        }
}
void work_area_fit() {
    for (const auto& work : {std::pair{1024,768}, {1366,728}, {1920,1040}, {3840,2080}})
        for(unsigned dpi : {96u,144u,192u,288u,384u})
            for(bool minimum : {false,true}) {
                const int chrome_width=static_cast<int>(16*dpi/96), chrome_height=static_cast<int>(40*dpi/96);
                const auto size=zw::ui::fit_client(work.first,work.second,dpi,chrome_width,chrome_height,minimum);
                CHECK(size.width+chrome_width<=work.first && size.height+chrome_height<=work.second);
                CHECK(zw::ui::calculate(size.width,size.height,dpi).valid);
            }
    CHECK(zw::ui::calculate(960,650,144).show_logo==false);
    CHECK(zw::ui::calculate(640,568,96).show_logo);
    const auto narrow=zw::ui::calculate(320,300,192); CHECK(narrow.campaign.y<narrow.practice.y);
    CHECK(!zw::ui::calculate(319,300,96).valid && !zw::ui::calculate(320,299,96).valid);
}
void keyboard_routing() {
    for(int action : {10,11,12}) {
        CHECK(zw::ui::resolve_command(1,action,false)==action);
        CHECK(zw::ui::resolve_command(action,10,false)==action);
        CHECK(zw::ui::resolve_command(1,action,true)==0);
        CHECK(zw::ui::resolve_command(action,action,true)==0);
    }
    CHECK(zw::ui::resolve_command(2,10,true)==2 && zw::ui::resolve_command(2,10,false)==2);
    CHECK(zw::ui::resolve_command(99,10,false)==0 && zw::ui::resolve_command(1,99,false)==0);
}
int main() {
    try { bounds_and_sections(); std::cout<<"PASS launcher controls fit and never overlap across window sizes/DPI\n";
        work_area_fit(); std::cout<<"PASS screen fit, compact header, narrow actions and minimum bounds\n";
        keyboard_routing(); std::cout<<"PASS default actions and busy keyboard routing\n3/3 launcher layout groups passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL launcher layout: "<<error.what()<<'\n'; return 1; }
}
