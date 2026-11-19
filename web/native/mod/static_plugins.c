#include <kh/kh_mod.h>

/* The table contract is defined in the PC port's runtime/plugins/plugins.c. */
typedef struct KhdaysStaticPlugin {
    const char *mod_id;
    KhPluginQuery query;
} KhdaysStaticPlugin;

extern const KhPluginDescriptor *khdays_pragmicat_query(void);

const KhdaysStaticPlugin khdays_static_plugins[] = {
    {"local.pragmicat-mix", khdays_pragmicat_query},
};
const int khdays_static_plugin_count = 1;
