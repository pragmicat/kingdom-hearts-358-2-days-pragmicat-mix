/* Browser replacement for khdays-port v0.1.5 runtime/core/clock.c.
 * Cooperative, single-host-thread Asyncify execution. No Windows clock APIs,
 * compositor-rate approximation, busy spinning or blocking browser waits. */
#include "clock.h"

#include <emscripten.h>
#include <math.h>
#include <stdint.h>

static double started_ms;
static double held_at_ms;
static double held_ms;
static unsigned held_reasons;
static int wake_pending;
static u64 last_cycles;

void khdays_clock_init(void)
{
    started_ms = emscripten_get_now();
    /* Browser visibility may already own a hold before native main enters.
     * Initialization establishes a new epoch without releasing that owner. */
    held_at_ms = held_reasons ? started_ms : 0.0;
    held_ms = 0.0;
    wake_pending = 0;
    last_cycles = 0;
}

u64 khdays_clock_cycles(void)
{
    const double now = held_reasons ? held_at_ms : emscripten_get_now();
    const double elapsed = fmax(0.0, now - started_ms - held_ms);
    const u64 cycles = (u64)(elapsed * ((double)KHDAYS_CLOCK_HZ / 1000.0));
    /* performance.now is monotonic. Also keep its rounding from ever giving a
     * caller an older cycle at a hold/resume boundary. */
    if (cycles > last_cycles) last_cycles = cycles;
    return last_cycles;
}

double khdays_clock_rate(void) { return 1.0; }

/* There is no DWM compositor timestamp in this host. Retain the exact DS
 * hardware rate instead of claiming an unmeasured browser screen lock. */
void khdays_clock_lock_screen(int on) { (void)on; }
void khdays_clock_vblank(u64 vblank) { (void)vblank; }

void khdays_clock_hold(unsigned reason, int on)
{
    if (reason == 0) return;
    if (on) {
        if (held_reasons == 0) held_at_ms = emscripten_get_now();
        held_reasons |= reason;
    } else if (held_reasons & reason) {
        held_reasons &= ~reason;
        if (held_reasons == 0) held_ms += emscripten_get_now() - held_at_ms;
    }
}

int khdays_clock_held(unsigned reason) { return (held_reasons & reason) != 0; }
void khdays_clock_wake(void) { wake_pending = 1; }

static void sleep_until(u64 cycle, int wakeable)
{
    for (;;) {
        const u64 now = khdays_clock_cycles();
        double left_ms;
        int delay;
        if (now >= cycle) return;
        /* Match upstream's auto-reset event: plain/deadline-complete waits do
         * not consume a wake; an early wake remains available for the next
         * genuinely waiting wakeable call. */
        if (wakeable && wake_pending) {
            wake_pending = 0;
            return;
        }
        left_ms = held_reasons ? 4.0 : (double)(cycle - now) * 1000.0 / (double)KHDAYS_CLOCK_HZ;
        delay = left_ms >= 4.0 ? 4 : left_ms >= 1.0 ? (int)left_ms : 1;
        /* This MUST suspend through Asyncify. It gives the browser its input,
         * audio and visibility callbacks; a CPU spin would freeze all three. */
        emscripten_sleep((unsigned)delay);
    }
}

void khdays_clock_sleep_until(u64 cycle) { sleep_until(cycle, 0); }
void khdays_clock_sleep_until_or_woken(u64 cycle) { sleep_until(cycle, 1); }
