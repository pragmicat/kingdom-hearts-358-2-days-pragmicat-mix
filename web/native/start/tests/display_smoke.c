#include "host/app/host.h"
#include "frame_bridge.h"
#include "../../profile-services/profile_start.h"

#include <emscripten.h>

static int wakes;
static int profile_loads;

/* Verify the call into the clock contract independently from its scheduler. */
void khdays_clock_wake(void)
{
    ++wakes;
}

void khdays_browser_apply_profile(void)
{
    ++profile_loads;
}

EMSCRIPTEN_KEEPALIVE int days_web_test_host_start(void)
{
    const int previously_loaded = profile_loads;
    const int started = khdays_host_start();
    return profile_loads - previously_loaded == started ? started : -1;
}

EMSCRIPTEN_KEEPALIVE int days_web_test_host_frame(void)
{
    static uint32_t upper[DAYS_WEB_SCREEN_PIXELS];
    static uint32_t lower[DAYS_WEB_SCREEN_PIXELS];
    if (khdays_host_frame_wants() != 0 || khdays_host_frame_begin() != NULL ||
        khdays_host_frame_widen() != 1.0f || khdays_host_frame_scale() != 1 ||
        khdays_host_graphics_flags() != KHDAYS_HOST_MOVIES_DS) {
        return 0;
    }
    upper[0] = 0x00ffac10u;
    lower[0] = 0x000c13ffu;
    khdays_host_present(upper, lower);
    khdays_host_frame_publish(NULL);
    khdays_host_wake();
    return wakes == 1;
}

EMSCRIPTEN_KEEPALIVE void days_web_test_no_data(void)
{
    khdays_host_tell_no_data(NULL, NULL);
    khdays_host_tell_no_data("data/extracted/sample", "corrupt manifest");
}

EMSCRIPTEN_KEEPALIVE void days_web_test_host_close(void)
{
    khdays_host_close();
}
