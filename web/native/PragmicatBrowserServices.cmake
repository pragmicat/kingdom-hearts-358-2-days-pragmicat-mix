# Add the original mission/room builder, cartridge and save services, native
# sound and Lua game services to an Emscripten target. The caller also adds the
# game objects, engine host, combat plugin and remaining platform services.
function(pragmicat_add_browser_services target upstream stb dr_libs)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "The Days browser services require Emscripten")
    endif()
    if(NOT TARGET SDL3::SDL3 OR NOT EXISTS "${stb}/stb_vorbis.c" OR
       NOT EXISTS "${dr_libs}/dr_flac.h")
        message(FATAL_ERROR "Provide the Days release's SDL3 target, stb and dr_libs sources")
    endif()

    set(port "${upstream}/port")
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/profile-services/profile_start.c"
        "${port}/host/app/profile.c"
        "${port}/host/app/hud_settings.c"
        "${port}/host/screens/ui_text.c"
        "${port}/runtime/core/rom.cpp"
        "${port}/runtime/core/card.c"
        "${port}/runtime/core/arm7.c"
        "${port}/runtime/core/pxi.c"
        "${port}/runtime/core/origin.c"
        "${port}/runtime/core/sha1.c"
        "${port}/runtime/core/sha256.c"
        "${port}/runtime/core/text_patches.cpp"
        "${port}/runtime/menus/saveslots.c"
        "${port}/runtime/menus/autosave.c"
        "${port}/runtime/menus/savelist.c"
        "${port}/runtime/menus/saveworld.c"
        "${port}/runtime/missions/missions.c"
        "${port}/runtime/missions/build.cpp"
        "${port}/runtime/missions/field_store.c"
        "${port}/runtime/missions/registry.c"
        "${port}/runtime/missions/save_part.c"
        "${port}/runtime/missions/tables.c"
        "${port}/runtime/missions/worlds.c"
        "${port}/runtime/menus/mission_start.c"
        "${port}/runtime/menus/retry.c"
        "${port}/runtime/menus/select_pages.c"
        "${port}/runtime/characters/characters.c"
        "${port}/runtime/characters/forms.c"
        "${port}/runtime/characters/moveset.c"
        "${port}/runtime/characters/packed.cpp"
        "${port}/runtime/characters/registry.c"
        "${port}/runtime/characters/rules.c"
        "${port}/runtime/characters/stats.c"
        "${port}/runtime/rooms/added.cpp"
        "${port}/runtime/rooms/slots.c"
        "${upstream}/studio/core/missions/mission_build.cpp"
        "${upstream}/studio/core/missions/missions.cpp"
        "${upstream}/studio/core/missions/mission_db.cpp"
        "${upstream}/studio/core/missions/command_names.cpp"
        "${upstream}/studio/core/files/containers.cpp"
        "${upstream}/studio/core/files/lz.cpp"
        "${upstream}/studio/core/files/paths.cpp"
        "${upstream}/assets/src/message.cpp"
        "${upstream}/assets/src/filesystem.cpp"
        "${port}/host/render/png_io.c"
        "${port}/runtime/mods/list.c"
        "${port}/runtime/mods/days.c"
        "${port}/runtime/mods/zip_read.c"
        "${port}/runtime/mods/textures.c"
        "${port}/runtime/mods/sheet_colours.c"
        "${port}/runtime/net/net_session.c"
        "${port}/runtime/net/roomlist.c"
        "${port}/runtime/net/mp_predict.c"
        "${port}/runtime/net/msg_overflow.c"
        "${port}/host/net/net_browser.c"
        "${port}/runtime/sound/sound.c"
        "${port}/runtime/sound/sound_bridge.c"
        "${port}/runtime/sound/sound_mods.c")

    set(sound_sources
        archive bus categories channels mix_ds mix_pc render replace
        snd7_alarm snd7_bank snd7_channel snd7_command snd7_driver snd7_hw
        snd7_os snd7_seq snd7_tables stream_reader third_party_decoders)
    list(TRANSFORM sound_sources PREPEND "${port}/sound/")
    list(TRANSFORM sound_sources APPEND ".c")
    target_sources(${target} PRIVATE ${sound_sources})

    set(script_sources
        scripts.c sandbox.c manifest.c order.c folders.cpp mods_log.c queue.c view.c
        api_events.c api_mod.c api_state.c api_menus.c api_actions.c api_v2.c
        api_enemies.c api_run.c api_screens.c api_deck.c api_time.c api_memory.c
        screens.c game_link.c deck_link.c time_link.c enemies_link.c enemy_names.c)
    list(TRANSFORM script_sources PREPEND "${port}/runtime/scripts/")
    target_sources(${target} PRIVATE ${script_sources})

    target_include_directories(${target} PRIVATE
        "${port}" "${port}/runtime" "${upstream}/assets/include"
        "${upstream}/sdk/include" "${upstream}/studio"
        "${stb}" "${dr_libs}")
    target_link_libraries(${target} PRIVATE SDL3::SDL3)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/lua/PragmicatBrowserLua.cmake")
    pragmicat_add_browser_lua(${target})
endfunction()
