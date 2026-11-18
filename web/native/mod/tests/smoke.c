#include <emscripten.h>
#include <kh/kh_mod.h>
#include <stdlib.h>
#include <string.h>
#include "host/app/files.h"

typedef struct KhdaysStaticPlugin {
    const char *mod_id;
    KhPluginQuery query;
} KhdaysStaticPlugin;

extern const KhdaysStaticPlugin khdays_static_plugins[];
extern const int khdays_static_plugin_count;

EMSCRIPTEN_KEEPALIVE int pragmicat_check_builtin(void)
{
    if (khdays_static_plugin_count != 1 ||
        strcmp(khdays_static_plugins[0].mod_id, "local.pragmicat-mix") != 0) {
        return 0;
    }
    const KhPluginDescriptor *plugin = khdays_static_plugins[0].query();
    return plugin && plugin->abi_major == KH_ABI_MAJOR && plugin->start &&
        plugin->stop && plugin->start(NULL) == KH_UNSUPPORTED;
}

EMSCRIPTEN_KEEPALIVE int pragmicat_is_practice(void)
{
    const char *practice = getenv("PRAGMICAT_PRACTICE");
    return practice && strcmp(practice, "1") == 0;
}

EMSCRIPTEN_KEEPALIVE int pragmicat_save_probe(const char *contents)
{
    return khdays_host_write_file_atomic("saves/slots/probe.sav", contents,
                                         strlen(contents));
}
