#include "host/app/files.h"

#include <emscripten.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

EM_ASYNC_JS(int, commit_save, (const char *path, const void *data, size_t size), {
    if (typeof Module.pragmicatCommitSave !== 'function') return 0;
    try {
        const bytes = HEAPU8.slice(data, data + size);
        await Module.pragmicatCommitSave(UTF8ToString(path), bytes);
        return 1;
    } catch (error) {
        console.error('Days save failed:', error.message);
        return 0;
    }
});

int khdays_host_write_file_atomic(const char *path, const void *data, size_t size)
{
    return path && (data || size == 0) && commit_save(path, data, size);
}

long khdays_host_read_file(const char *path, void *data, size_t room)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    size_t count = fread(data, 1, room > LONG_MAX ? LONG_MAX : room, file);
    const int failed = ferror(file);
    fclose(file);
    return failed ? -1 : (long)count;
}

void khdays_host_local_time(int *hour, int *minute, int *second)
{
    const time_t now = time(NULL);
    const struct tm *local = localtime(&now);
    *hour = local ? local->tm_hour : 0;
    *minute = local ? local->tm_min : 0;
    *second = local ? local->tm_sec : 0;
}
