#include "host/screens/ui_text.h"
#include "runtime/scripts/view.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>

EM_JS(void, show_notice, (int warning, const char *line), {
    if (Module["onDaysModNotice"]) Module["onDaysModNotice"](warning, UTF8ToString(line));
});
EM_JS_DEPS(mod_notices, "$UTF8ToString");

/* Called by the frame presenter. Keep the latest gameplay cue and diagnostic
 * independently so a burst of script notices cannot bury a mod failure. */
EMSCRIPTEN_KEEPALIVE void days_web_drain_mod_notices(void)
{
    int kind;
    char *mod, *text;
    char lines[2][512];
    int received[2] = {0};
    while (khdays_scripts_take_notice(&kind, &mod, &text)) {
        const int warning = kind != KHDAYS_NOTICE_SAID;
        const char *reason = text ? text : "";
        const char *format = "%s";
        char formatted[400];
        switch (kind) {
        case KHDAYS_NOTICE_STOPPED:
            format = khdays_text(TXT_MOD_STOPPED);
            break;
        case KHDAYS_NOTICE_PAUSED:
            format = khdays_text(TXT_MOD_PAUSED);
            break;
        case KHDAYS_NOTICE_REFUSED:
            format = khdays_text(TXT_MOD_REFUSED);
            break;
        }
        snprintf(formatted, sizeof(formatted), format, reason);
        if (warning && mod && *mod)
            snprintf(lines[warning], sizeof(lines[warning]), "%s: %s", mod, formatted);
        else
            snprintf(lines[warning], sizeof(lines[warning]), "%s", formatted);
        received[warning] = 1;
        free(mod);
        free(text);
    }
    for (int warning = 0; warning <= 1; ++warning) {
        if (received[warning]) show_notice(warning, lines[warning]);
    }
}
