#include "frame_bridge.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t upper[DAYS_WEB_SCREEN_PIXELS], lower[DAYS_WEB_SCREEN_PIXELS];
static uint8_t io[0x2000], palette[0x800], oam[0x800];
static KhdaysVramPages vram;

static void store16(unsigned offset, unsigned value)
{
    io[offset] = (uint8_t)value;
    io[offset + 1] = (uint8_t)(value >> 8);
}

static void check_pixel(const uint8_t *rgba, unsigned pixel, unsigned r, unsigned g, unsigned b)
{
    assert(rgba[pixel * 4] == r);
    assert(rgba[pixel * 4 + 1] == g);
    assert(rgba[pixel * 4 + 2] == b);
    assert(rgba[pixel * 4 + 3] == 255);
}

int main(void)
{
    const uint8_t *rgba = days_web_frame_rgba();
    days_web_reset_renderer();
    upper[0] = 0x00123456;
    upper[DAYS_WEB_SCREEN_PIXELS - 1] = 0x00abcdef;
    lower[0] = 0x00ff0080;
    lower[DAYS_WEB_SCREEN_PIXELS - 1] = 0x00010203;
    assert(days_web_present(upper, lower));
    check_pixel(rgba, 0, 0x12, 0x34, 0x56);
    check_pixel(rgba, DAYS_WEB_SCREEN_PIXELS - 1, 0xab, 0xcd, 0xef);
    check_pixel(rgba, DAYS_WEB_SCREEN_PIXELS, 0xff, 0, 0x80);
    check_pixel(rgba, DAYS_WEB_SCREEN_PIXELS * 2 - 1, 1, 2, 3);
    assert(days_web_frame_generation() == 1);
    assert(!days_web_present(NULL, lower));
    assert(days_web_frame_generation() == 1);

    KhdaysG3dList list = {0};
    KhdaysGpuInput input = {0};
    input.io = io;
    input.palette = palette;
    input.oam = oam;
    input.vram = &vram;
    /* Engine A: normal graphics, BG0 from the upstream 3D rasterizer. */
    io[1] = 1; /* BG0 enabled */
    io[0] = 8; /* engine A BG0 is 3D */
    io[2] = 1; /* normal display */
    /* Engine B: normal graphics, backdrop green (BGR555=0x03e0). */
    io[0x1002] = 1;
    palette[0x400] = 0xe0;
    palette[0x401] = 0x03;
    /* Opaque red 3D rear plane, alpha 31 and far clear depth. */
    store16(0x350, 31);
    store16(0x352, 31);
    store16(0x354, 0x7fff);
    assert(days_web_render(&list, &input, 0));
    check_pixel(rgba, 0, 255, 0, 0);
    check_pixel(rgba, DAYS_WEB_SCREEN_PIXELS - 1, 255, 0, 0);
    check_pixel(rgba, DAYS_WEB_SCREEN_PIXELS, 0, 255, 0);
    assert(days_web_render(&list, &input, 1));
    check_pixel(rgba, 0, 0, 255, 0);
    check_pixel(rgba, DAYS_WEB_SCREEN_PIXELS, 255, 0, 0);
    assert(days_web_frame_generation() == 3);
    /* A real native polygon through CPU scan conversion and DS composition. */
    KhdaysG3dVertex vertices[3] = {
        {.x = 20, .y = 20, .z = 0, .w = 4096, .g = 63, .b = 63, .a = 1},
        {.x = 220, .y = 20, .z = 0, .w = 4096, .g = 63, .b = 63, .a = 1},
        {.x = 120, .y = 170, .z = 0, .w = 4096, .g = 63, .b = 63, .a = 1},
    };
    KhdaysG3dPolygon polygon = {.attr = 31u << 16, .count = 3, .ymin = 20, .ymax = 170};
    list.npolys = 1;
    list.nverts = 3;
    list.polys = &polygon;
    list.verts = vertices;
    assert(days_web_render(&list, &input, 0));
    check_pixel(rgba, 80 * DAYS_WEB_WIDTH + 120, 0, 255, 255);
    check_pixel(rgba, 0, 255, 0, 0);
    assert(days_web_frame_generation() == 4);
    assert(!days_web_render(&list, &input, 2));
    assert(!days_web_render(NULL, &input, 0));
    KhdaysG3dPolygon invalid = {0};
    invalid.count = 4; /* more vertices than the list contains */
    list.polys = &invalid;
    assert(!days_web_render(&list, &input, 0));
    invalid.count = 11; /* beyond gpu3d.c's ten interpolation slots */
    list.nverts = 11;
    assert(!days_web_render(&list, &input, 0));
    assert(days_web_frame_generation() == 4);
    days_web_reset_renderer();
    assert(days_web_frame_generation() == 0);
    puts("upstream CPU rendering + pixel conversion: passed");
    return 0;
}
