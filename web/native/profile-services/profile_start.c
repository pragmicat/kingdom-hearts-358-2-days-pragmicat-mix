#include "profile_start.h"

#include "host/app/profile.h"
#include "host/app/sound_output.h"
#include "host/screens/ui_text.h"
#include "runtime/menus/savelist.h"

#include <stddef.h>

void khdays_browser_apply_profile(void)
{
    char path[512];
    KhdaysProfile profile;
    khdays_settings_path(path, sizeof(path));
    khdays_profile_load(&profile, path, NULL);
    khdays_ui_set_language(khdays_language_choose(profile.language));
    khdays_sound_output_set(&profile.sound);
    khdays_savelist_set_settings(profile.save.all_files, profile.save.autosave);
}
