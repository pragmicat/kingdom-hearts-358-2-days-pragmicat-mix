/* The real khdays-port host PCM callbacks and cooperative sound service.
 * WebAudio owns device creation/resume, explicitly from the player's gesture.
 * This C component never creates a browser AudioContext. */
#include "audio_emscripten.h"
#include "host.h"
#include "sound_output.h"
#include "profile.h"
#include "clock.h"
#include "runtime/sound/sound_bridge.h"
#include "sound/mix_ds.h"

#include <emscripten.h>
#include <stddef.h>
#include <stdint.h>

#define DAYS_AUDIO_CHUNK_FRAMES 512
#define DAYS_AUDIO_MAX_CALL_FRAMES 8192

static int pump_running;
static int output_open;
static int output_paused;
static int output_muted;
static int float_rate;
static int pumping;

EM_JS(void, days_web_audio_forward, (const void *samples, int frames, int rate, int format), {
    if (typeof Module['onDaysAudio'] !== 'function') return;
    try { Module['onDaysAudio'](samples, frames, rate, format); }
    catch (error) {
        if (typeof Module['onDaysAudioProblem'] === 'function') Module['onDaysAudioProblem'](String(error));
        else console.warn('Days audio block was dropped:', error);
    }
});
EM_JS(void, days_web_audio_control, (int action, int enabled), {
    if (typeof Module['onDaysAudioControl'] === 'function') Module['onDaysAudioControl'](action, enabled);
});

/* A host device must not hold the native program alive after it exits. */
EM_JS(void, days_web_audio_timer_start, (void), {
    const interval = setInterval(() => {
        if (ABORT) {
            clearInterval(interval);
            delete Module['daysAudioTimer'];
        } else {
            Module['_days_web_audio_pump']();
        }
    }, 1);
    Module['daysAudioTimer'] = interval;
});

EM_JS(void, days_web_audio_timer_stop, (void), {
    clearInterval(Module['daysAudioTimer']);
    delete Module['daysAudioTimer'];
});

static int can_forward(const void *frames, int count)
{
    return output_open && !output_paused && !output_muted && frames != NULL &&
           count > 0 && count <= DAYS_AUDIO_MAX_CALL_FRAMES;
}

void khdays_host_audio(const int16_t *frames, int count)
{
    if (!can_forward(frames, count)) return;
    while (count > 0) {
        const int chunk = count > DAYS_AUDIO_CHUNK_FRAMES ? DAYS_AUDIO_CHUNK_FRAMES : count;
        days_web_audio_forward(frames, chunk, KHDAYS_SOUND_RATE, 0);
        frames += chunk * 2;
        count -= chunk;
    }
}

void khdays_host_audio_float(const float *frames, int count)
{
    if (!float_rate || !can_forward(frames, count)) return;
    while (count > 0) {
        const int chunk = count > DAYS_AUDIO_CHUNK_FRAMES ? DAYS_AUDIO_CHUNK_FRAMES : count;
        days_web_audio_forward(frames, chunk, float_rate, 1);
        frames += chunk * 2;
        count -= chunk;
    }
}

EMSCRIPTEN_KEEPALIVE void days_web_audio_set_device_rate(int rate)
{
    float_rate = rate >= 8000 && rate <= 192000 ? rate : 0;
}

EMSCRIPTEN_KEEPALIVE void days_web_audio_pump(void)
{
    if (!pump_running || pumping) return;
    pumping = 1;
    khdays_sound_bridge_advance(khdays_clock_cycles());
    pumping = 0;
}

void khdays_host_sound_start(void)
{
    /* Browser WebAudio resamples this exact native DS mixer. No synthetic
     * replacement music/effects or unverified PC-mixer device rate is used. */
    khdays_sound_bridge_set_output(1, 0);
    if (!output_open) khdays_sound_output_open();
    if (!pump_running) {
        pump_running = 1;
        days_web_audio_timer_start();
    }
}

EMSCRIPTEN_KEEPALIVE void days_web_audio_stop(void)
{
    if (pump_running) days_web_audio_timer_stop();
    pump_running = 0;
    output_open = output_paused = output_muted = 0;
    days_web_audio_control(0, 0); /* open/closed */
}

void khdays_sound_output_open(void)
{
    if (output_open) return;
    output_open = 1;
    output_paused = output_muted = 0;
    days_web_audio_control(0, 1);
}
void khdays_sound_output_close(void) { days_web_audio_stop(); }
void khdays_sound_output_pause(int paused)
{
    output_paused = paused != 0;
    days_web_audio_control(1, output_paused);
}
void khdays_sound_output_mute(int mute)
{
    output_muted = mute != 0;
    days_web_audio_control(2, output_muted);
}
void khdays_sound_output_set(const KhdaysSound *settings)
{
    if (settings == NULL) return;
    /* The driver applies these levels once, before PCM reaches WebAudio. */
    khdays_sound_bridge_set_output(1, 0);
    khdays_sound_bridge_set_volumes(settings->volume, settings->music,
                                    settings->effects, settings->voices, settings->movies);
}
