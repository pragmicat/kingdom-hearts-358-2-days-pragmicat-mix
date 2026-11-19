#define _POSIX_C_SOURCE 200809L
#include "runtime/input/input.h"
#include "runtime/input/controls_hooks.h"
#include "runtime/input/analog.h"
#include "runtime/core/window.h"
#include "runtime/core/runtime.h"
#include "runtime/core/clock.h"
#include "runtime/core/session_log.h"
#include "runtime/net/net_session.h"
#include "host/app/context.h"
#include "input_emscripten.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Actual upstream hooks not exported by controls_hooks.h. */
extern int khdays_controls_move_heading(int actor, int heading);
extern int khdays_controls_move_rate(int actor, int rate);
extern int khdays_controls_move_anim(int actor, int scale);
extern int khdays_controls_selection_pressed(int pressed, int other_screen);
extern int khdays_controls_selection_repeat(int repeat, int pressed);
extern int khdays_controls_command_step(void);

#define ACTOR 0x02010000u
#define SCENE 0x0204bdb0u
#define XY 0x027fffa8u
#define DIRECTIONS (KHDAYS_KEY_UP|KHDAYS_KEY_DOWN|KHDAYS_KEY_LEFT|KHDAYS_KEY_RIGHT)
static unsigned hidden_buttons, plugin_remove;
static int movie;
static KhdaysNetStick shared;
static unsigned filters[16], filter_count;

/* Bounded unrelated services. No actual menu, plugin, camera or network game
 * is started. The production input/filter dispatch, sequencers, hooks and
 * compact DS memory are linked unchanged. */
unsigned char *khdays_window;
void khdays_context_stack_source(void *(*allocate)(size_t), void (*release)(void *, size_t)) { (void)allocate; (void)release; }
void khdays_log(const char *format, ...) { (void)format; }
void khdays_log_close(void) {}
void khdays_clock_hold(unsigned reason, int hold) { (void)reason; (void)hold; }
void khdays_clock_wake(void) {}
void khdays_autosave_frame(void) {}
void khdays_language_frame(void) {}
void khdays_cinelist_frame(void) {}
void khdays_daycard_frame(void) {}
unsigned long long khdays_diag_symbol(const char *name) { return strcmp(name,"FixtureUpdate") == 0 ? 0x1234u : 0; }
void khdays_net_set_stick(const KhdaysNetStick *stick) { shared=*stick; }
int khdays_net_stick(unsigned aid, KhdaysNetStick *out) { (void)aid; (void)out; return 0; }
int QueryActiveStateOrDelegate(void) { return 0; }
int GetEntryField20ByIndex(int index) { return index == 0 ? (int)ACTOR : 0; }
int khdays_kh2_camera_active(void) { return 0; }
void khdays_kh2_camera_center(void) {}
void khdays_kh2_camera_toggle_first_person(void) {}
int khdays_kh2_camera_claims(int a,int b,int c) { (void)a;(void)b;(void)c;return 0; }
void khdays_kh2_camera_player_state(int state) { (void)state; }
void khdays_models_actor_state(int actor,int state) { (void)actor;(void)state; }
uint16_t khdays_movie_hd_game_buttons(uint16_t buttons) { return movie ? 0 : buttons; }
int khdays_movie_hd_running(void) { return movie; }
unsigned khdays_plugins_hidden_buttons(void) { return hidden_buttons; }
#define FILTER(name,number) uint16_t name(uint16_t buttons) { if(filter_count<16) filters[filter_count++]=number; return buttons; }
FILTER(khdays_newgame_buttons,1)
FILTER(khdays_savelist_buttons,2)
FILTER(khdays_cinelist_buttons,3)
FILTER(khdays_roomlist_buttons,4)
FILTER(khdays_charselect_buttons,5)
FILTER(khdays_camp_buttons,6)
FILTER(khdays_retry_buttons,7)
FILTER(khdays_scripts_buttons,8)
FILTER(khdays_mission_start_buttons,9)
unsigned khdays_plugins_buttons(unsigned buttons) { if(filter_count<16) filters[filter_count++]=10; return buttons & ~plugin_remove; }

static unsigned sample(void)
{
    filter_count=0;
    khdays_input_pad_sample();
    assert(filter_count==10);
    for(unsigned i=0;i<10;++i) assert(filters[i]==i+1);
    return (~khdays_input_keyinput()) & 0x3ffu;
}
static unsigned xy(void) { return *(uint16_t *)KHDAYS_W(XY); }

static void taps(void)
{
    days_web_input_buttons(KHDAYS_KEY_A|KHDAYS_KEY_X|KHDAYS_KEY_Y);
    days_web_input_buttons(0);
    assert(sample()==KHDAYS_KEY_A);
    assert(xy()==0x2000u); /* X and Y held in the actual ARM7 RAM latch. */
    assert(khdays_input_keyinput()==0x03ffu); /* later reads are instantaneous */
    assert(khdays_input_xy()==0x2c00u);
    assert(sample()==0 && xy()==0x2c00u);
    days_web_input_buttons(KHDAYS_KEY_X); days_web_input_buttons(0);
    assert(sample()==0 && xy()==0x2800u);
    assert(sample()==0 && xy()==0x2c00u);
    days_web_input_buttons(KHDAYS_KEY_Y); days_web_input_buttons(0);
    assert(sample()==0 && xy()==0x2400u);
}
static void repress(void)
{
    days_web_input_buttons(KHDAYS_KEY_A);
    assert(sample()==KHDAYS_KEY_A);
    days_web_input_buttons(0); days_web_input_buttons(KHDAYS_KEY_A);
    assert(sample()==0); /* release before the new press */
    assert(sample()==KHDAYS_KEY_A);
    assert(sample()==KHDAYS_KEY_A); /* held is still held */
}
static void ownership(void)
{
    int x,y;
    days_web_input_ui_open(1);
    days_web_input_buttons(KHDAYS_KEY_A|KHDAYS_KEY_X);
    days_web_input_touch(400,-10,1);
    days_web_input_sticks(24000,-18000,10000,5000,1);
    assert(sample()==0 && xy()==0x2c00u);
    assert(!khdays_input_touch(&x,&y));
    assert(khdays_input_snapshot()->move_x==0 && khdays_input_snapshot()->move_y==0);
    days_web_input_ui_open(0);
    assert(sample()==0); /* no replay from the menu boundary */
    days_web_input_touch(400,-10,1);
    assert(khdays_input_touch(&x,&y) && x==255 && y==0);
    movie=1;
    days_web_input_buttons(KHDAYS_KEY_A|KHDAYS_KEY_Y);
    assert(sample()==0 && xy()==0x2c00u && !khdays_input_touch(&x,&y));
    movie=0; plugin_remove=KHDAYS_KEY_Y;
    assert(sample()==KHDAYS_KEY_A && xy()==0x2c00u); /* plugin filtering precedes XY write */
}
static void scripts(void)
{
    int x,y;
    khdays_input_frame(0); assert(sample()==KHDAYS_KEY_A);
    khdays_input_frame(1); assert(sample()==KHDAYS_KEY_A); /* two game reads before release */
    khdays_input_frame(2); assert(sample()==0);
    assert(khdays_input_touch(&x,&y) && x==42 && y==73);
    khdays_input_frame(3); assert(!khdays_input_touch(&x,&y));
    khdays_input_frame(4); sample();
    assert(khdays_input_snapshot()->move_x==16000 && khdays_input_snapshot()->move_y==8000);
    assert(khdays_input_snapshot()->actions & KHDAYS_ACTION_LOCK_ON);
    khdays_input_frame(5); sample();
    assert(khdays_input_snapshot()->camera_x==20000 && khdays_input_snapshot()->camera_y==-9000);
    assert(!(khdays_input_snapshot()->actions & KHDAYS_ACTION_LOCK_ON));
}
static void scripted_ui(void)
{
    int x,y;
    days_web_input_ui_open(1);
    khdays_input_frame(0);
    assert(sample()==KHDAYS_KEY_A); /* upstream diagnostic scripts intentionally bypass host UI ownership */
    assert(khdays_input_touch(&x,&y) && x==7 && y==8);
}
static void anchors(void)
{
    khdays_input_frame(0); assert(sample()==0);
    *(unsigned *)KHDAYS_W(SCENE)=2;
    khdays_input_frame(1); assert(sample()==0 && xy()==0x2800u);
    khdays_input_frame(2); assert(sample()==0 && xy()==0x2800u);
    khdays_input_frame(3); assert(sample()==0 && xy()==0x2c00u);
    khdays_input_update_ran(0x9999);
    khdays_input_frame(4); assert(sample()==0);
    khdays_input_update_ran(0x1234);
    khdays_input_frame(5); assert(sample()==KHDAYS_KEY_B);
}
static void analog_hooks(void)
{
    /* Native controls need a DS direction before replacing its heading. The
     * browser adapter's missing stick-to-D-pad synthesis is a separate host
     * defect, so this hook test explicitly supplies the native direction. */
    const int dpad=0xdfff;
    days_web_input_sticks(18000,-10000,0,0,1);
    days_web_input_buttons(KHDAYS_KEY_UP|KHDAYS_KEY_RIGHT);
    sample();
    assert(khdays_controls_move_heading(ACTOR,dpad)==dpad); /* previous snapshot is still idle */
    sample();
    const KhdaysInputSnapshot *previous=khdays_input_previous_snapshot();
    const int heading=khdays_analog_heading_turned(khdays_analog_heading(previous->move_x,previous->move_y),dpad);
    assert(khdays_controls_move_heading(ACTOR,dpad)==heading && heading!=dpad);
    assert(khdays_controls_move_heading(ACTOR,-1)==-1);
    const int speed=khdays_controls_move_rate(ACTOR,1000);
    assert(speed==khdays_analog_rate(1000,khdays_analog_tilt(previous->move_x,previous->move_y),previous->run_threshold,previous->walk_speed));
    assert(khdays_controls_move_anim(ACTOR,4096)==4096*speed/1000);
    hidden_buttons=DIRECTIONS;
    sample(); assert(khdays_input_snapshot()->move_x==0 && khdays_input_snapshot()->move_y==0);
    assert(shared.x==0 && shared.y==0);
    sample(); assert(khdays_controls_move_heading(ACTOR,dpad)==dpad);
    assert(khdays_controls_selection_pressed(KHDAYS_KEY_R,1)==KHDAYS_KEY_R);
    assert(khdays_controls_selection_pressed(0,0)==KHDAYS_KEY_R);
    khdays_controls_lock_let_go();
    assert(khdays_controls_selection_repeat(0x9000,KHDAYS_KEY_R)==0);
}
int main(int argc,char **argv)
{
    const char *mode=argc>1 ? argv[1] : "taps";
    if(!strcmp(mode,"scripts")) setenv("KHDAYS_INPUT","0+a 1-a 2@42,73 3@- 4~16000,8000 4+LoCk_On 5^20000,-9000 5-Lock_On",1);
    else if(!strcmp(mode,"anchors")) setenv("KHDAYS_INPUT","s2 0+x 1-x uFixtureUpdate 0+b 1-b",1);
    else if(!strcmp(mode,"scripts-ui")) setenv("KHDAYS_INPUT","0+a 0@7,8",1);
    else if(!strcmp(mode,"bad-script")) setenv("KHDAYS_INPUT","0@bad",1);
    else unsetenv("KHDAYS_INPUT");
    assert(khdays_memory_map());
    days_web_input_reset(); khdays_input_init();
    if(!strcmp(mode,"taps")) taps();
    else if(!strcmp(mode,"repress")) repress();
    else if(!strcmp(mode,"ownership")) ownership();
    else if(!strcmp(mode,"scripts")) scripts();
    else if(!strcmp(mode,"anchors")) anchors();
    else if(!strcmp(mode,"scripts-ui")) scripted_ui();
    else if(!strcmp(mode,"analog")) analog_hooks();
    else abort();
    khdays_memory_unmap();
    printf("production browser input + mapped DS RAM: %s passed\n",mode);
    return 0;
}
