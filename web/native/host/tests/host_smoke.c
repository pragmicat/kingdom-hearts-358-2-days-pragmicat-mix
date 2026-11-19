#include "clock.h"
#include "context.h"
#include "host.h"
#include "input_emscripten.h"
#include "instance.h"
#include "bindings.h"
#include "runtime/input/input.h"
#include "runtime/input/analog.h"
#include "runtime/net/net_session.h"
#include <stdint.h>

#include <assert.h>
#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const u64 MS_CYCLES = KHDAYS_CLOCK_HZ / 1000u;
static KhdaysContext *root, *game;
static int callback_ran;
static void wake(void *argument) { (void)argument; callback_ran++; khdays_clock_wake(); }
static void mark(void *argument) { (void)argument; callback_ran++; }
static void sleep_ms(unsigned ms) { emscripten_sleep(ms); }

static void clock_test(void)
{
    u64 at, held;
    double before;
    days_web_host_hidden(1); /* attachment may run before native main */
    khdays_clock_init();
    assert(khdays_clock_held(DAYS_WEB_HOLD_HIDDEN));
    sleep_ms(5);
    assert(khdays_clock_cycles() == 0);
    days_web_host_hidden(0);
    at = khdays_clock_cycles();
    assert(at < MS_CYCLES * 10);
    sleep_ms(10);
    assert(khdays_clock_cycles() >= at + MS_CYCLES * 5);
    khdays_clock_lock_screen(1);
    khdays_clock_vblank(0);
    assert(khdays_clock_rate() == 1.0);

    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
    held = khdays_clock_cycles();
    sleep_ms(8);
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1); /* duplicate does not restart hold */
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1);
    sleep_ms(8);
    assert(khdays_clock_cycles() == held);
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
    assert(khdays_clock_held(KHDAYS_HOLD_MOVIE));
    sleep_ms(8);
    assert(khdays_clock_cycles() == held);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    assert(khdays_clock_cycles() - held < MS_CYCLES * 5);
    sleep_ms(8);
    assert(khdays_clock_cycles() >= held + MS_CYCLES * 4);
    khdays_clock_hold(0, 1);
    assert(!khdays_clock_held(~0u));

    /* An early wake survives a plain wait, then is consumed exactly once. */
    khdays_clock_wake();
    khdays_clock_sleep_until(khdays_clock_cycles() + MS_CYCLES * 6);
    at = khdays_clock_cycles();
    before = emscripten_get_now();
    khdays_clock_sleep_until_or_woken(at + MS_CYCLES * 1000);
    assert(emscripten_get_now() - before < 50.0);
    assert(khdays_clock_cycles() < at + MS_CYCLES * 1000);
    callback_ran = 0;
    emscripten_async_call(wake, NULL, 8);
    before = emscripten_get_now();
    khdays_clock_sleep_until_or_woken(khdays_clock_cycles() + MS_CYCLES * 1000);
    assert(callback_ran == 1 && emscripten_get_now() - before >= 4.0);
    assert(emscripten_get_now() - before < 500.0);

    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
    days_web_host_hidden(1);
    days_web_host_hidden(0);
    assert(khdays_clock_held(KHDAYS_HOLD_SETTINGS));
    assert(!khdays_clock_held(DAYS_WEB_HOLD_HIDDEN));
    held = khdays_clock_cycles();
    /* Hidden/show wakes are latched; consume them before the timed wake. */
    khdays_clock_sleep_until_or_woken(held + MS_CYCLES);
    callback_ran = 0;
    emscripten_async_call(wake, NULL, 8);
    khdays_clock_sleep_until_or_woken(held + MS_CYCLES * 1000);
    assert(callback_ran == 1 && khdays_clock_cycles() == held);
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
    puts("Clock: elapsed cycles, nested holds, visibility and latched/asynchronous wakes passed");
}

static void input_test(void)
{
    KhdaysInputSnapshot sample;
    int x, y;
    days_web_input_reset();
    days_web_input_buttons(1);
    days_web_input_buttons(0);
    khdays_host_input_take(&sample);
    assert(sample.ds_buttons == 1 && sample.ds_buttons_field == 1);
    khdays_host_input_take(&sample);
    assert(sample.ds_buttons == 0);
    days_web_input_buttons(1);
    khdays_host_input_take(&sample);
    assert(sample.ds_buttons == 1);
    days_web_input_buttons(0);
    days_web_input_buttons(1);
    khdays_host_input_take(&sample);
    assert(sample.ds_buttons == 0);
    khdays_host_input_take(&sample);
    assert(sample.ds_buttons == 1);
    days_web_input_buttons(~0u);
    assert(khdays_host_buttons() == 0x0fff);
    days_web_input_touch(999, -50, 1);
    assert(khdays_host_touch(&x, &y) && x == 255 && y == 0);
    days_web_input_sticks(-40000, 40000, -40000, 40000, 1);
    khdays_host_input_take(&sample);
    assert(sample.move_x == -32767 && sample.move_y == -32767);
    assert(sample.camera_x < -23000 && sample.camera_x > -23200 && sample.camera_y == sample.camera_x);
    assert(sample.sticks[0][1] == 32767);
    assert(sample.enhancements == (KHDAYS_ENH_ANALOG_MOVEMENT | KHDAYS_ENH_ANALOG_CAMERA));
    days_web_input_sticks(1000, -1000, 0, 0, 1);
    khdays_host_input_take(&sample);
    assert(!sample.move_x && !sample.move_y && sample.sticks[0][0] == 1000);
    days_web_input_sticks(32767, 0, 0, 0, 1);
    khdays_host_input_take(&sample);
    assert(sample.move_x == 32767 && !sample.move_y);
    days_web_input_ui_open(1);
    days_web_input_buttons(1);
    days_web_input_touch(1, 1, 1);
    khdays_host_input_take(&sample);
    assert(sample.ui_open && !sample.ds_buttons && !sample.touching && !sample.move_x);
    days_web_input_ui_open(0);
    khdays_host_input_take(&sample);
    assert(!sample.ui_open && !sample.ds_buttons); /* no stale menu tap */
    days_web_input_buttons(1);
    days_web_host_hidden(1);
    khdays_host_input_take(&sample);
    assert(!sample.ds_buttons); /* hidden boundary cancels unconsumed tap */
    days_web_host_hidden(0);
    assert(!khdays_host_touch(NULL, NULL));
    khdays_host_input_take(NULL);
    puts("Input: actual upstream snapshot/press-edge ABI, touches, analog bounds and ownership passed");
}

/* Real native movement hook and original D-pad routine, with a local actor
 * fixture. No game/runtime control flow is substituted in these two functions. */
static unsigned char actor_bytes[64];
static KhdaysInputSnapshot movement_previous;
unsigned char *khdays_window;
int QueryActiveStateOrDelegate(void) { return 0; }
int GetEntryField20ByIndex(int index) { return index == 0 ? (int)(uintptr_t)actor_bytes : 0; }
u32 khdays_input_game_frames(void) { return 1; }
const KhdaysInputSnapshot *khdays_input_previous_snapshot(void) { return &movement_previous; }
int khdays_net_stick(unsigned int member, KhdaysNetStick *out) { (void)member; (void)out; return 0; }
extern int Ov022_HeadingFromHeldDpad(int actor);
extern int khdays_controls_move_heading(int actor, int heading);

static void analog_input_test(void)
{
    static const int directions[8][2] = {{32767,0},{32767,-32767},{0,-32767},{-32767,-32767},
                                       {-32767,0},{-32767,32767},{0,32767},{32767,32767}};
    const KhdaysStick native_config = {KHDAYS_STICK_DPAD,20,5};
    int native_active = 0, native_sector = -1;
    KhdaysInputSnapshot sample;
    days_web_input_ui_open(0);
    days_web_input_reset();
    for (unsigned i=0;i<8;++i) {
        const int x=directions[i][0], y=directions[i][1];
        const unsigned wanted=khdays_stick_dpad(x,y,&native_config,&native_active,&native_sector);
        days_web_input_sticks(x,y,0,0,1);
        assert(khdays_host_buttons()==wanted && wanted!=0);
    }
    /* Compare native angular hysteresis continuously across sector boundaries. */
    for (int x=-32000;x<=32000;x+=4000) for (int y=-32000;y<=32000;y+=4000) {
        const unsigned wanted=khdays_stick_dpad(x,y,&native_config,&native_active,&native_sector);
        days_web_input_sticks(x,y,0,0,1);
        assert(khdays_host_buttons()==wanted);
    }
    days_web_input_reset();
    days_web_input_buttons(KHDAYS_KEY_A|KHDAYS_KEY_UP);
    days_web_input_sticks(32767,0,0,0,1);
    assert(khdays_host_buttons()==(KHDAYS_KEY_A|KHDAYS_KEY_UP|KHDAYS_KEY_RIGHT));
    days_web_input_buttons(KHDAYS_KEY_A); /* button callback cannot erase stick */
    assert(khdays_host_buttons()==(KHDAYS_KEY_A|KHDAYS_KEY_RIGHT));
    days_web_input_sticks(0,0,0,0,0); /* pad disconnect cannot erase keyboard */
    assert(khdays_host_buttons()==KHDAYS_KEY_A);
    days_web_input_buttons(0);
    assert(!khdays_host_buttons());

    days_web_input_sticks(6200,0,0,0,1); assert(!khdays_host_buttons());
    days_web_input_sticks(7000,0,0,0,1); assert(khdays_host_buttons()==KHDAYS_KEY_RIGHT);
    days_web_input_sticks(5500,0,0,0,1); assert(khdays_host_buttons()==KHDAYS_KEY_RIGHT);
    khdays_host_input_take(&sample); assert(sample.move_x==5500); /* raw native tilt during hysteresis */
    days_web_input_sticks(4500,0,0,0,1); assert(!khdays_host_buttons());
    days_web_input_reset();
    days_web_input_sticks(0,0,32767,0,1); assert(!khdays_host_buttons()); /* camera alone never walks */
    days_web_input_reset();
    days_web_input_sticks(24000,-17000,0,0,1);
    khdays_host_input_take(&movement_previous);
    memcpy(actor_bytes+0x1a,&movement_previous.ds_buttons_field,sizeof(uint16_t));
    const int actor=(int)(uintptr_t)actor_bytes;
    const int native_heading=Ov022_HeadingFromHeldDpad(actor);
    assert(native_heading!=-1); /* reproduces the broken browser host before this fix */
    assert(khdays_controls_move_heading(actor,native_heading)==khdays_analog_heading(24000,17000));

    days_web_input_ui_open(1);
    days_web_input_reset(); /* canvas/window blur owns physical input only */
    days_web_input_buttons(KHDAYS_KEY_A);
    days_web_input_sticks(32767,0,0,0,1);
    khdays_host_input_take(&sample);
    assert(sample.ui_open && !sample.ds_buttons && !sample.move_x);
    days_web_host_hidden(1); days_web_host_hidden(0);
    khdays_host_input_take(&sample);
    assert(sample.ui_open && !sample.ds_buttons); /* tab visibility cannot close a menu */
    days_web_input_ui_open(0);
    khdays_host_input_take(&sample); assert(!sample.ui_open && !sample.ds_buttons && !sample.move_x);
    puts("Analog: real native D-pad hysteresis/heading hook, mixed input and physical/menu ownership passed");
}

static void game_entry(void *argument)
{
    volatile unsigned local = 12345;
    (void)argument;
    callback_ran = 0;
    emscripten_async_call(mark, NULL, 1);
    khdays_clock_sleep_until(khdays_clock_cycles() + MS_CYCLES * 12);
    assert(callback_ran == 1 && local == 12345);
    assert(khdays_context_current() == game);
    khdays_context_switch(root);
    for (;;) khdays_context_switch(root);
}

int main(void)
{
    unsigned short name[8];
    clock_test();
    input_test();
    analog_input_test();
    assert(khdays_host_player_slot_taken() == 0);
    assert(khdays_host_player_slot() == 1 && khdays_host_player_slot_taken() == 1);
    assert(strcmp(khdays_host_user_folder(), "saves") == 0);
    assert(khdays_host_player_name("Roxas", name, 3) == 3 && name[0] == 'R' && name[2] == 'x');
    assert(khdays_host_player_name("x", NULL, 4) == 0);
    assert(!khdays_host_other_copies());
    root = khdays_context_init();
    assert(root);
    game = khdays_context_create(game_entry, NULL);
    assert(game);
    khdays_context_switch(game);
    assert(khdays_context_current() == root);
    khdays_context_delete(game);
    puts("Host Wasm: Asyncify timer callbacks run inside the actual Days fiber context");
    return 0;
}
