#define _POSIX_C_SOURCE 200809L
#include "host/app/host.h"
#include "host/app/instance.h"
#include "host/app/profile.h"
#include "host/app/sound_output.h"
#include "host/screens/ui_text.h"
#include "runtime/menus/savelist.h"
#include "../profile_start.h"

#include <SDL3/SDL_iostream.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int khdays_pragmicat_select_mode(int practice);

static KhdaysSound applied_sound;
static int applied_all_files, applied_autosave;

void khdays_sound_output_set(const KhdaysSound *sound)
{
    applied_sound = *sound;
}

void khdays_savelist_set_settings(int all_files, int autosave)
{
    applied_all_files = all_files;
    applied_autosave = autosave;
}

static void write_settings(const char *path, const char *text)
{
    assert(SDL_SaveFile(path, text, strlen(text)));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const int practice = strcmp(argv[1], "practice") == 0;
    assert(practice || strcmp(argv[1], "campaign") == 0);
    assert(khdays_pragmicat_select_mode(practice));
    assert(khdays_host_player_slot() == 1);

    /* Recreate only the local filesystem layout of the browser launcher. */
    assert(mkdir("/profiles", 0700) == 0);
    assert(mkdir("/profiles/campaign", 0700) == 0);
    assert(mkdir("/profiles/practice", 0700) == 0);
    assert(mkdir("/persistent", 0700) == 0);
    assert(mkdir("/persistent/campaign", 0700) == 0);
    assert(mkdir("/persistent/practice", 0700) == 0);
    assert(symlink("/persistent/campaign", "/profiles/campaign/saves") == 0);
    assert(symlink("/persistent/practice", "/profiles/practice/saves") == 0);
    write_settings("/persistent/campaign/settings.ini",
                   "version = 1\nlanguage = fr\n"
                   "[network]\nnickname = CampRoxas\nserver = campaign.invalid\nonline = off\n"
                   "[save]\nfiles = 3\nautosave = played\n"
                   "[sound]\nvolume = 36\nmusic = 20\neffects = 58\nvoices = 72\nmovies = 90\n"
                   "[enhancements]\nhd_font = off\n");
    write_settings("/persistent/practice/settings.ini",
                   "version = 1\nlanguage = de\n"
                   "[network]\nnickname = DashRoxas\nserver = practice.invalid\nonline = on\n"
                   "[save]\nfiles = 99\nautosave = file99\n"
                   "[sound]\nvolume = 71\nmusic = 26\neffects = 90\nvoices = 40\nmovies = 55\n"
                   "[enhancements]\nhd_font = on\n");
    assert(chdir(practice ? "/profiles/practice" : "/profiles/campaign") == 0);

    char path[512], nickname[64], server[64];
    KhdaysProfile profile;
    const int language = practice ? KHDAYS_LANGUAGE_GERMAN : KHDAYS_LANGUAGE_FRENCH;
    khdays_settings_path(path, sizeof(path));
    assert(strcmp(path, "saves/settings.ini") == 0);
    assert(khdays_settings_game_language() == language);
    khdays_settings_nickname(nickname, sizeof(nickname));
    assert(strcmp(nickname, practice ? "DashRoxas" : "CampRoxas") == 0);
    assert(khdays_settings_server(server, sizeof(server)) == practice);
    assert(strcmp(server, practice ? "practice.invalid" : "campaign.invalid") == 0);
    khdays_profile_load(&profile, path, NULL);
    assert(profile.language == language);
    khdays_browser_apply_profile();
    assert(khdays_ui_language() == language);
    assert(profile.save.all_files == practice);
    assert(profile.save.autosave == (practice ? KHDAYS_AUTOSAVE_FILE99 : KHDAYS_AUTOSAVE_PLAYED));
    assert(applied_all_files == profile.save.all_files && applied_autosave == profile.save.autosave);
    assert(profile.sound.volume == (practice ? 71 : 36));
    assert(profile.sound.music == (practice ? 26 : 20));
    assert(profile.sound.effects == (practice ? 90 : 58));
    assert(profile.sound.voices == (practice ? 40 : 72));
    assert(profile.sound.movies == (practice ? 55 : 90));
    assert(applied_sound.volume == profile.sound.volume && applied_sound.music == profile.sound.music &&
           applied_sound.effects == profile.sound.effects && applied_sound.voices == profile.sound.voices &&
           applied_sound.movies == profile.sound.movies);
    khdays_hud_settings_publish(profile.enh.hud);
    assert(khdays_hud_setting(KHDAYS_OPT_FONT) == practice);

    const int default_mode = practice ? KHDAYS_AUTOSAVE_OFF : KHDAYS_AUTOSAVE_FILE99;
    write_settings(path, "version = 1\n[save]\nfiles = 3\n");
    khdays_profile_load(&profile, path, NULL);
    assert(profile.save.autosave == default_mode && !profile.save.all_files);
    write_settings(path, "version = 1\n[save]\nautosave = bogus\n");
    khdays_profile_load(&profile, path, NULL);
    assert(profile.save.autosave == default_mode);
    write_settings(path, "malformed profile\n");
    khdays_profile_load(&profile, path, NULL);
    assert(profile.save.autosave == default_mode);
    assert(access("saves/settings.ini.bad", F_OK) == 0);
    assert(unlink(path) == 0);
    khdays_profile_load(&profile, path, NULL);
    assert(profile.save.autosave == default_mode);
    khdays_browser_apply_profile();
    assert(applied_autosave == default_mode);
    assert(access(practice ? "/persistent/campaign/settings.ini" : "/persistent/practice/settings.ini", F_OK) == 0);
    printf("%s local profile language, nickname, server, volume, HUD, and save modes passed.\n", argv[1]);
    return 0;
}
