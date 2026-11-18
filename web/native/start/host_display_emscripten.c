#include "host/app/host.h"
#include "runtime/core/clock.h"
#include "../profile-services/profile_start.h"

#include <emscripten.h>
#include <stddef.h>
#include <stdio.h>

/* Canvas receives frames through renderer/frame_bridge.c. This is the host
 * side of the original runtime's CPU composition path. */
EM_JS(int, days_web_frame_sink_ready, (void), {
    return typeof Module["onDaysFrame"] === "function" ? 1 : 0;
});

EM_JS(void, days_web_report_problem, (const char *where, const char *problem), {
    if (typeof Module["onDaysRuntimeProblem"] === "function") {
        Module["onDaysRuntimeProblem"](UTF8ToString(where), UTF8ToString(problem));
    }
});

EM_JS(void, days_web_notify_closed, (void), {
    if (typeof Module["onDaysRuntimeClosed"] === "function") {
        Module["onDaysRuntimeClosed"]();
    }
});

static int started;

int khdays_host_start(void)
{
    if (!days_web_frame_sink_ready()) {
        const char *problem = "Attach the Days framebuffer before starting the recompilation.";
        fprintf(stderr, "khdays-browser: %s\n", problem);
        days_web_report_problem("Canvas", problem);
        return 0;
    }
    khdays_browser_apply_profile();
    started = 1;
    return 1;
}

void khdays_host_tell_no_data(const char *where, const char *broken)
{
    const char *folder = where ? where : "data/extracted";
    const char *problem = broken ? broken : "Choose a supported Days ROM before starting.";
    fprintf(stderr, "khdays-browser: %s: %s\n", folder, problem);
    days_web_report_problem(folder, problem);
}

void khdays_host_close(void)
{
    if (started) {
        started = 0;
        days_web_notify_closed();
    }
}

void khdays_host_wake(void)
{
    khdays_clock_wake();
}

int khdays_host_frame_wants(void)
{
    return 0;
}

float khdays_host_frame_widen(void)
{
    return 1.0f;
}

int khdays_host_frame_scale(void)
{
    return 1;
}

int khdays_host_graphics_flags(void)
{
    return KHDAYS_HOST_MOVIES_DS;
}

KhdaysFramePackage *khdays_host_frame_begin(void)
{
    return NULL;
}

void khdays_host_frame_publish(KhdaysFramePackage *package)
{
    /* No separate GPU package was requested or allocated. */
    (void)package;
}
