/* Only the upstream test fixture includes this adapter, never the runtime. */
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PATH 260
#define FILE_ATTRIBUTE_READONLY 1u
#define FILE_ATTRIBUTE_NORMAL 0u

static unsigned GetTempPathA(unsigned size, char *out)
{
    return (unsigned)snprintf(out, size, "/tmp/");
}

static unsigned GetCurrentProcessId(void) { return 1; }

static int CreateDirectoryA(const char *directory, void *security)
{
    (void)security;
    return mkdir(directory, 0700) == 0;
}

EM_JS(void, days_test_refuse_sync, (int refuse), {
    Module.daysTestRefuseSync = !!refuse;
});

static int SetFileAttributesA(const char *path, unsigned attributes)
{
    (void)path;
    days_test_refuse_sync(attributes == FILE_ATTRIBUTE_READONLY);
    return 1;
}

static int DeleteFileA(const char *path) { return unlink(path) == 0; }
static int RemoveDirectoryA(const char *path) { return rmdir(path) == 0; }
