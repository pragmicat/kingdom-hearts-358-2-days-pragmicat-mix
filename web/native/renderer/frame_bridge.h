/* Browser presentation for the upstream native port's CPU renderer. */
#ifndef DAYS_WEB_FRAME_BRIDGE_H
#define DAYS_WEB_FRAME_BRIDGE_H

#include <stdint.h>
#include "gpu/gpu2d.h"
#include "gpu/gpu3d.h"

#define DAYS_WEB_WIDTH 256u
#define DAYS_WEB_SCREEN_HEIGHT 192u
#define DAYS_WEB_HEIGHT (DAYS_WEB_SCREEN_HEIGHT * 2u)
#define DAYS_WEB_SCREEN_PIXELS (DAYS_WEB_WIDTH * DAYS_WEB_SCREEN_HEIGHT)
#define DAYS_WEB_RGBA_BYTES (DAYS_WEB_WIDTH * DAYS_WEB_HEIGHT * 4u)

/* The buffer remains owned by the module and changes on the next presentation.
 * Bytes are explicitly R,G,B,A, independent of host endianness. */
const uint8_t *days_web_frame_rgba(void);
uint32_t days_web_frame_generation(void);
int days_web_present(const uint32_t *upper, const uint32_t *lower);

/* Draw an actual upstream polygon list and both DS display engines in software.
 * upper_engine is engine A (0) or engine B (1), matching POWCNT1 bit 15.
 * Input points to the native port's register, palette, OAM and VRAM snapshots.
 * Call serially on the game thread: upstream CPU rasterizer state is shared. */
int days_web_render(const KhdaysG3dList *list, const KhdaysGpuInput *input, unsigned upper_engine);
void days_web_reset_renderer(void);

#endif
