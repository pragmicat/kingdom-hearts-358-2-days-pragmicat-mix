#ifndef DAYS_WEB_INPUT_EMSCRIPTEN_H
#define DAYS_WEB_INPUT_EMSCRIPTEN_H

#include "host.h"

/* Functions called by browser-input.mjs. All run on the one cooperative host
 * thread. The real upstream KhdaysInputSnapshot and press-edge implementation
 * remain authoritative; no separately packed JavaScript struct is used. */
void days_web_input_reset(void);
void days_web_input_buttons(unsigned buttons);
void days_web_input_touch(int x, int y, int down);
void days_web_input_sticks(int lx, int ly, int rx, int ry, int enabled);
void days_web_input_ui_open(int open);

/* Browser visibility owns a distinct hold bit, so showing a tab cannot release
 * settings/movie holds. This is a platform-only reason, not a game menu. */
#define DAYS_WEB_HOLD_HIDDEN (1u << 16)
void days_web_host_hidden(int hidden);

#endif
