/* Exercise the original day-card unit's browser timing guard without assets. */
#include "runtime/menus/daycard.c"

int main(void)
{
    if (browser_card_active()) {
        return 1;
    }
    khdays_daycard_hd_drawn();
    if (!browser_card_active()) {
        return 2;
    }
    hd_drawn_at = emscripten_get_now() - (2.0 * HD_RECENT);
    return browser_card_active() ? 3 : 0;
}
