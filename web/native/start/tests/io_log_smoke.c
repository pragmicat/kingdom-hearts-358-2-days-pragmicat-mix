#define _POSIX_C_SOURCE 200809L
#include "runtime/core/card.h"
#include "runtime/core/files.h"
#include "runtime/core/rom.h"
#include "runtime/core/session_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 last_offset, calls;

void khdays_rom_read(u32 offset, u8 *destination, u32 size)
{
    ++calls;
    last_offset = offset;
    for (u32 i = 0; i < size; ++i) {
        destination[i] = (u8)(offset + i);
    }
}

int khdays_rom_area_at(u32 offset, char *name, int size, u32 *start, u32 *end)
{
    if (offset < 0x1000 || offset >= 0x2000) {
        return 0;
    }
    snprintf(name, (size_t)size, "%s", "fixture.bin");
    *start = 0x1000;
    *end = 0x2000;
    return 1;
}

void khdays_card_reads(u32 *commands, u32 *bytes)
{
    static int first = 1;
    *commands = first ? 2u : 0u;
    *bytes = first ? 1024u : 0u;
    first = 0;
}

static int check_limit(void)
{
    char message[1100];
    memset(message, 'x', sizeof(message));
    message[sizeof(message) - 1] = '\0';
    khdays_log_want(1);
    for (unsigned i = 0; i < 3000; ++i) {
        khdays_log("%s", message);
    }
    if (khdays_log_on()) {
        return 1;
    }
    khdays_log_want(1);
    return khdays_log_on() ? 2 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "limit") == 0) {
        return check_limit();
    }
    if (argc > 1) {
        if (setenv("KHDAYS_FILES", argv[1], 1) != 0) return 8;
        const int expected = strcmp(argv[1], "check") == 0 ? KHDAYS_FILES_CHECK : KHDAYS_FILES_CARD;
        return khdays_files_mode() == expected ? 0 : 3;
    }
    if (khdays_files_mode() != KHDAYS_FILES_PORT || khdays_files_chip_id() != KHDAYS_CARD_CHIP_ID ||
        khdays_log_on() || khdays_log_marks() != 0) {
        return 4;
    }
    khdays_log_want(1);
    khdays_log_frame(17);
    u8 data[512];
    khdays_files_read(0x1002, data, 3);
    if (memcmp(data, "\x02\x03\x04", 3) != 0) {
        return 5;
    }
    khdays_files_read(0x1009, data, 1);
    khdays_files_read(0x100c, data, 0);
    khdays_files_card_page(0x341, data);
    if (calls != 4 || last_offset != 0x8141 || data[0] != 0x41 || data[511] != 0x40) {
        return 6;
    }
    khdays_files_log_reads();
    khdays_files_log_reads();
    khdays_files_second();
    khdays_files_second();
    khdays_log_mark();
    khdays_log_want(0);
    if (khdays_log_on() || khdays_log_marks() != 1) {
        return 7;
    }
    khdays_log("marker after disabled");
    return 0;
}
