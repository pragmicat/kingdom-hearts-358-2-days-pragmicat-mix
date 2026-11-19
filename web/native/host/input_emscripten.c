/* The actual upstream host input ABI, fed by browser events. */
#include "input_emscripten.h"
#include "press_edges.h"
#include "clock.h"
#include "stick_dpad.h"

#include <emscripten.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static KhdaysInputSnapshot input;
static KhdaysPressEdges edges, edges_field;
/* Keep hardware buttons distinct from the stick's DS directions. Either
 * source can update first without erasing the other source's held input. */
static uint16_t physical_buttons, movement_buttons;
static int movement_active, movement_sector = -1;
static const KhdaysStick movement_config = {KHDAYS_STICK_DPAD, 20, 5};

static int axis(int value) { return value < -32767 ? -32767 : value > 32767 ? 32767 : value; }
static unsigned bounded(int value, unsigned high) { return value < 0 ? 0u : (unsigned)value > high ? high : (unsigned)value; }
static void generation(void) { ++input.generation; }

EMSCRIPTEN_KEEPALIVE void days_web_input_reset(void)
{
    const uint32_t next = input.generation + 1u;
    const uint8_t ui_open = input.ui_open;
    memset(&input, 0, sizeof(input));
    memset(&edges, 0, sizeof(edges));
    memset(&edges_field, 0, sizeof(edges_field));
    physical_buttons = movement_buttons = 0;
    movement_active = 0;
    movement_sector = -1;
    input.generation = next;
    /* Focus/visibility owns physical state, not the host menu's ownership. */
    input.ui_open = ui_open;
    input.camera_speed = input.mouse_speed = 100;
    input.run_threshold = 75;
    input.walk_speed = 55;
}

static void publish_buttons(void)
{
    const uint16_t before = input.ds_buttons;
    const uint16_t before_field = input.ds_buttons_field;
    input.ds_buttons = input.ui_open ? 0 : (uint16_t)(physical_buttons | movement_buttons);
    input.ds_buttons_field = input.ds_buttons;
    khdays_press_edges_note(&edges, before, input.ds_buttons);
    khdays_press_edges_note(&edges_field, before_field, input.ds_buttons_field);
}

EMSCRIPTEN_KEEPALIVE void days_web_input_buttons(unsigned buttons)
{
    physical_buttons = (uint16_t)(buttons & 0x0fffu);
    publish_buttons();
    generation();
}

EMSCRIPTEN_KEEPALIVE void days_web_input_touch(int x, int y, int down)
{
    input.touch_x = (uint16_t)bounded(x, 255u);
    input.touch_y = (uint16_t)bounded(y, 191u);
    input.touching = down && !input.ui_open;
    generation();
}

static void filtered_stick(int x, int y, int16_t *out_x, int16_t *out_y)
{
    const double distance = sqrt((double)x * x + (double)y * y);
    const double dead = 32767.0 * 0.20;
    double scale;
    if (distance <= dead) { *out_x = *out_y = 0; return; }
    scale = fmin(1.0, (distance - dead) / (32767.0 - dead)) * 32767.0 / distance;
    *out_x = (int16_t)(x * scale);
    *out_y = (int16_t)(y * scale);
}

EMSCRIPTEN_KEEPALIVE void days_web_input_sticks(int lx, int ly, int rx, int ry, int enabled)
{
    const int active = enabled && !input.ui_open;
    input.sticks[0][0] = active ? (int16_t)axis(lx) : 0;
    input.sticks[0][1] = active ? (int16_t)axis(ly) : 0;
    input.sticks[1][0] = active ? (int16_t)axis(rx) : 0;
    input.sticks[1][1] = active ? (int16_t)axis(ry) : 0;
    movement_buttons = days_web_stick_dpad(input.sticks[0][0], input.sticks[0][1],
                                           &movement_config, &movement_active, &movement_sector);
    /* Native controls first require a held DS direction. Then they use the
     * same stick's exact angle and raw tilt, including its hysteresis tail. */
    input.move_x = movement_buttons ? input.sticks[0][0] : 0;
    input.move_y = movement_buttons ? (int16_t)-input.sticks[0][1] : 0;
    publish_buttons();
    filtered_stick(input.sticks[1][0], -input.sticks[1][1], &input.camera_x, &input.camera_y);
    input.enhancements = active ? KHDAYS_ENH_ANALOG_MOVEMENT | KHDAYS_ENH_ANALOG_CAMERA : 0;
    generation();
}

EMSCRIPTEN_KEEPALIVE void days_web_input_ui_open(int open)
{
    /* Clear latched taps on a host/menu boundary. Releasing a host menu must
     * never replay an attack entered while that menu owned the controls. */
    days_web_input_reset();
    input.ui_open = open != 0;
}

EMSCRIPTEN_KEEPALIVE void days_web_host_hidden(int hidden)
{
    if (hidden) days_web_input_reset();
    khdays_clock_hold(DAYS_WEB_HOLD_HIDDEN, hidden != 0);
    khdays_clock_wake();
}

void khdays_host_input_take(KhdaysInputSnapshot *out)
{
    if (out == NULL) return;
    *out = input;
    out->ds_buttons = khdays_press_edges_take(&edges, input.ds_buttons);
    out->ds_buttons_field = khdays_press_edges_take(&edges_field, input.ds_buttons_field);
}

uint16_t khdays_host_buttons(void) { return input.ds_buttons; }
int khdays_host_touch(int *x, int *y)
{
    if (!input.touching || x == NULL || y == NULL) return 0;
    *x = input.touch_x;
    *y = input.touch_y;
    return 1;
}

void khdays_host_latency_sampled(uint32_t vblank) { (void)vblank; }
