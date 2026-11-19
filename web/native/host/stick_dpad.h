/* Pure stick-to-D-pad arithmetic extracted unchanged from khdays-port
 * v0.1.5 host/app/bindings.c, commit2037f79. Private names avoid a new host
 * linker dependency. The upstream MIT notice is ../UPSTREAM_LICENSE.txt.
 * Keep behavior matched by the real-upstream comparison regression. */
#ifndef DAYS_WEB_STICK_DPAD_H
#define DAYS_WEB_STICK_DPAD_H
#include "profile.h"
#include <math.h>
#define DAYS_WEB_DS_BIT(action) ((uint16_t)(1u << (action)))
/* How far a stick is pushed, in percent of its travel (can pass 100 in the
 * corners of a square gate). */
static inline double days_web_stick_percent(int x, int y)
{
    return sqrt((double)x * x + (double)y * y) * 100.0 / 32767.0;
}

/* The stick's angle in degrees, 0 = right, counterclockwise (up = 90). SDL's
 * y grows downwards, so it is negated. */
static inline double days_web_stick_degrees(int x, int y)
{
    double deg = atan2(-(double)y, (double)x) * 180.0 / 3.14159265358979323846;
    return deg < 0 ? deg + 360.0 : deg;
}

/* The D-pad bits of each of the eight directions, counterclockwise from right. */
static const uint16_t days_web_sector_bits[8] = {
    DAYS_WEB_DS_BIT(KHDAYS_ACT_RIGHT), DAYS_WEB_DS_BIT(KHDAYS_ACT_RIGHT) | DAYS_WEB_DS_BIT(KHDAYS_ACT_UP),
    DAYS_WEB_DS_BIT(KHDAYS_ACT_UP),    DAYS_WEB_DS_BIT(KHDAYS_ACT_UP) | DAYS_WEB_DS_BIT(KHDAYS_ACT_LEFT),
    DAYS_WEB_DS_BIT(KHDAYS_ACT_LEFT),  DAYS_WEB_DS_BIT(KHDAYS_ACT_LEFT) | DAYS_WEB_DS_BIT(KHDAYS_ACT_DOWN),
    DAYS_WEB_DS_BIT(KHDAYS_ACT_DOWN),  DAYS_WEB_DS_BIT(KHDAYS_ACT_DOWN) | DAYS_WEB_DS_BIT(KHDAYS_ACT_RIGHT),
};

static inline uint16_t days_web_stick_dpad(int x, int y, const KhdaysStick *stick, int *active, int *sector)
{
    const double pushed = days_web_stick_percent(x, y);
    /* Past the dead zone to start moving; back below it, less the
     * hysteresis, to stop, so a stick resting on the edge does not flicker. */
    if (!*active && pushed >= stick->dead_zone) {
        *active = 1;
    } else if (*active && pushed < stick->dead_zone - stick->hysteresis) {
        *active = 0;
    }
    if (!*active) {
        *sector = -1;
        return 0;
    }
    {
        const double deg = days_web_stick_degrees(x, y);
        /* Each direction owns 45 degrees; the current one keeps the stick a
         * little past its edge (the hysteresis, in percent of those 45). */
        if (*sector >= 0) {
            double off = fabs(deg - *sector * 45.0);
            if (off > 180.0) {
                off = 360.0 - off;
            }
            if (off <= 22.5 + stick->hysteresis * 45.0 / 100.0) {
                return days_web_sector_bits[*sector];
            }
        }
        *sector = (int)floor((deg + 22.5) / 45.0) % 8;
        return days_web_sector_bits[*sector];
    }
}

#undef DAYS_WEB_DS_BIT
#endif
