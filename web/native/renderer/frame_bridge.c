#include "frame_bridge.h"

#include <stddef.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define DAYS_WEB_EXPORT EMSCRIPTEN_KEEPALIVE
EM_JS(void, days_web_notify_frame, (const uint8_t *pixels, uint32_t generation), {
    if (typeof Module["onDaysFrame"] === "function") {
        Module["onDaysFrame"](pixels, 256, 384, generation);
    }
});
#else
#define DAYS_WEB_EXPORT
static void days_web_notify_frame(const uint8_t *pixels, uint32_t generation)
{
    (void)pixels;
    (void)generation;
}
#endif

static uint8_t rgba[DAYS_WEB_RGBA_BYTES];
static uint32_t screens[2][DAYS_WEB_SCREEN_PIXELS];
static uint32_t layer3d[DAYS_WEB_SCREEN_PIXELS];
static KhdaysGpu2dState state[2];
static uint32_t generation;

DAYS_WEB_EXPORT const uint8_t *days_web_frame_rgba(void)
{
    return rgba;
}

DAYS_WEB_EXPORT uint32_t days_web_frame_generation(void)
{
    return generation;
}

static void convert_screen(uint8_t *out, const uint32_t *in)
{
    for (unsigned i = 0; i < DAYS_WEB_SCREEN_PIXELS; ++i) {
        const uint32_t color = in[i]; /* upstream final framebuffer: 0x00RRGGBB */
        out[i * 4u] = (uint8_t)(color >> 16);
        out[i * 4u + 1u] = (uint8_t)(color >> 8);
        out[i * 4u + 2u] = (uint8_t)color;
        out[i * 4u + 3u] = 255u;
    }
}

DAYS_WEB_EXPORT int days_web_present(const uint32_t *upper, const uint32_t *lower)
{
    if (upper == NULL || lower == NULL) {
        return 0;
    }
    convert_screen(rgba, upper);
    convert_screen(rgba + DAYS_WEB_SCREEN_PIXELS * 4u, lower);
    ++generation;
    days_web_notify_frame(rgba, generation);
    return 1;
}

/* Exact host callback used by runtime/display/display.c when frame_begin returns
 * NULL and frame_wants returns zero. This bridge does not start the game. */
void khdays_host_present(const uint32_t *upper, const uint32_t *lower)
{
    (void)days_web_present(upper, lower);
}

DAYS_WEB_EXPORT int days_web_render(const KhdaysG3dList *list, const KhdaysGpuInput *input, unsigned upper_engine)
{
    KhdaysGpuInput draw;
    if (list == NULL || input == NULL || input->io == NULL || input->palette == NULL ||
        input->oam == NULL || input->vram == NULL || upper_engine > 1u ||
        list->npolys < 0 || list->nverts < 0 ||
        (list->npolys != 0 && list->polys == NULL) || (list->nverts != 0 && list->verts == NULL)) {
        return 0;
    }
    for (int i = 0; i < list->npolys; ++i) {
        const KhdaysG3dPolygon *poly = &list->polys[i];
        /* gpu3d.c uses ten interpolation slots for a clipped polygon. */
        if (poly->first < 0 || poly->count < 0 || poly->count > 10 || poly->first > list->nverts ||
            poly->count > list->nverts - poly->first) {
            return 0;
        }
    }
    draw = *input;
    draw.layer3d = layer3d;
    khdays_gpu3d_draw(list, input->io, input->vram, layer3d);
    khdays_gpu2d_frame(&draw, &state[0], 0, screens[0], NULL);
    khdays_gpu2d_frame(&draw, &state[1], 1, screens[1], NULL);
    return days_web_present(screens[upper_engine], screens[1u - upper_engine]);
}

DAYS_WEB_EXPORT void days_web_reset_renderer(void)
{
    memset(state, 0, sizeof(state));
    memset(rgba, 0, sizeof(rgba));
    generation = 0;
}
