/* Synthetic channel samples through the ACTUAL pinned DS mixer and host ABI.
 * These bridge stubs record host-pump linkage; they do not emulate the ARM7
 * driver or establish game audio. No ROM samples are used. */
#include "audio_emscripten.h"
#include "host.h"
#include "sound_output.h"
#include "clock.h"
#include "context.h"
#include <assert.h>
#include "runtime/sound/sound_bridge.h"
#include "sound/channels.h"
#include "sound/mix_ds.h"

#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>

static unsigned advances, output_calls;
static u64 last_cycle;
static int exact_mode, output_rate;
static int volumes[5];
void khdays_sound_bridge_advance(u64 cycle) { ++advances; last_cycle = cycle; }
void khdays_sound_bridge_set_output(int exact, int rate) { ++output_calls; exact_mode = exact; output_rate = rate; }
void khdays_sound_bridge_set_volumes(int volume, int music, int effects, int voices, int movies)
{ volumes[0]=volume; volumes[1]=music; volumes[2]=effects; volumes[3]=voices; volumes[4]=movies; }

EMSCRIPTEN_KEEPALIVE void days_test_audio_start(void)
{
    khdays_clock_init();
    khdays_sound_output_open();
    khdays_host_sound_start();
    khdays_host_sound_start(); /* starts one cooperative interval */
}
EMSCRIPTEN_KEEPALIVE unsigned days_test_audio_advances(void) { return advances; }
EMSCRIPTEN_KEEPALIVE double days_test_audio_cycle(void) { return (double)last_cycle; }
EMSCRIPTEN_KEEPALIVE int days_test_audio_exact(void) { return output_calls > 0 && exact_mode == 1 && output_rate == 0; }

EMSCRIPTEN_KEEPALIVE void days_test_emit_mixer(int count)
{
    static int16_t samples[2 * 1025];
    KhdaysSoundHw hw = {0};
    if (count < 0 || count > 1025) return;
    khdays_sound_hw_reset(&hw, NULL);
    khdays_sound_hw_write(&hw, 0x100, 2, 0x807f);
    khdays_sound_hw_write(&hw, 0x104, 2, 0x200);
    khdays_sound_hw_write(&hw, 0x00, 4, 127); /* channel 0 fully left */
    khdays_sound_hw_write(&hw, 0x10, 4, 127 | (127u << 16)); /* channel 1 fully right */
    hw.channels[0].sample = 16384;
    hw.channels[1].sample = -16384;
    for (int i = 0; i < count; ++i) khdays_sound_mix_ds(&hw, NULL, 1.0f, &samples[i * 2]);
    khdays_host_audio(samples, count);
}
EMSCRIPTEN_KEEPALIVE void days_test_emit_float(void)
{
    const float samples[4] = {0.25f,-0.5f,1.5f,-2.0f};
    khdays_host_audio_float(samples, 2);
}
EMSCRIPTEN_KEEPALIVE void days_test_audio_pause(int paused) { khdays_sound_output_pause(paused); }
EMSCRIPTEN_KEEPALIVE void days_test_audio_mute(int muted) { khdays_sound_output_mute(muted); }
EMSCRIPTEN_KEEPALIVE void days_test_audio_invalid(void)
{
    const int16_t samples[2] = {1,2};
    khdays_host_audio(NULL, 1); khdays_host_audio(samples, -1);
    khdays_host_audio(samples, 8193);
}

static KhdaysContext *audio_root;
static int fiber_completed;
static void audio_fiber(void *argument)
{
    volatile unsigned local = 2468;
    const unsigned before = advances;
    (void)argument;
    khdays_clock_sleep_until(khdays_clock_cycles() + KHDAYS_CLOCK_HZ / 50u);
    assert(local == 2468 && advances > before);
    khdays_context_switch(audio_root);
    for (;;) khdays_context_switch(audio_root);
}
EMSCRIPTEN_KEEPALIVE int days_test_audio_fiber(void)
{
    audio_root = khdays_context_init();
    assert(audio_root);
    KhdaysContext *game = khdays_context_create(audio_fiber, NULL);
    assert(game);
    khdays_context_switch(game);
    assert(khdays_context_current() == audio_root);
    khdays_context_delete(game);
    fiber_completed = 1;
    return 1;
}

EMSCRIPTEN_KEEPALIVE int days_test_audio_fiber_completed(void) { return fiber_completed; }

EMSCRIPTEN_KEEPALIVE void days_test_audio_exit(void)
{
    days_test_audio_start();
    exit(7);
}
