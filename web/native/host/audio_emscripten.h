#ifndef DAYS_WEB_AUDIO_EMSCRIPTEN_H
#define DAYS_WEB_AUDIO_EMSCRIPTEN_H

/* Cooperative replacement for the Windows sound-output thread. The timer and
 * all bridge/game calls run on one Emscripten host thread. */
void days_web_audio_pump(void);
void days_web_audio_stop(void);
/* Float callbacks are meaningful only after the device's actual rate is known.
 * The initial browser path always selects upstream's DS-exact 32728 Hz mixer. */
void days_web_audio_set_device_rate(int rate);

#endif
