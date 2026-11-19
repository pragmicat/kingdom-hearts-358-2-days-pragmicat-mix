/* A Wasm module has one local player/profile. The startup helper owns its
 * selected working directory and mounted saves symlink before native entry. */
#include "instance.h"

#include <stdio.h>
#include <stdlib.h>

static int slot;
int khdays_host_player_slot(void) { slot = 1; return slot; }
int khdays_host_player_slot_taken(void) { return slot; }
const char *khdays_host_user_folder(void) { return "saves"; }
int khdays_host_other_copies(void) { return 0; }

void khdays_host_automatic_name(char *out, size_t size)
{
    if (out != NULL && size != 0) snprintf(out, size, "Player0000");
}

int khdays_host_player_name(const char *nickname, unsigned short *name, int room)
{
    const char *chosen = getenv("KHDAYS_NAME");
    int length = 0;
    if (name == NULL || room <= 0) return 0;
    if (chosen == NULL || chosen[0] == '\0') chosen = nickname;
    if (chosen == NULL || chosen[0] == '\0') chosen = "Player0000";
    for (; *chosen != '\0' && length < room; ++chosen) {
        const unsigned char value = (unsigned char)*chosen;
        if (value >= 0x20 && value < 0x7f) name[length++] = value;
    }
    return length;
}
