# Add the PC port's built-in plugin table and the native mod to an Emscripten
# game target. Each target must have exactly one static_plugins.c.
function(pragmicat_add_browser_mod target)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "Pragmicat's browser mod requires Emscripten")
    endif()

    get_filename_component(project_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
    include("${project_root}/cmake/PragmicatSources.cmake")
    list(TRANSFORM PRAGMICAT_COMBAT_SOURCES PREPEND "${project_root}/")
    add_library(pragmicat-browser-combat OBJECT ${PRAGMICAT_COMBAT_SOURCES})
    target_include_directories(pragmicat-browser-combat PRIVATE
        "${project_root}/vendor/sdk/include" "${project_root}/generated")
    target_compile_features(pragmicat-browser-combat PRIVATE cxx_std_20)
    target_compile_definitions(pragmicat-browser-combat PRIVATE
        kh_plugin_query=khdays_pragmicat_query)
    target_compile_options(pragmicat-browser-combat PRIVATE -fexceptions)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
    target_sources(${target} PRIVATE
        $<TARGET_OBJECTS:pragmicat-browser-combat>
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/static_plugins.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/profile_mode.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/files_emscripten.c")
    target_include_directories(${target} PRIVATE "${project_root}/vendor/sdk/include")
    target_link_options(${target} PRIVATE -fexceptions -sFORCE_FILESYSTEM=1 -lidbfs.js)

    # Embed each authored file by name. A directory glob could pull in a local
    # DLL, generated game data or someone else's settings.
    foreach(profile IN ITEMS campaign practice)
        foreach(file IN ITEMS mod.ini parameters.ini)
            pragmicat_embed_file(${target} "${project_root}/mod/${file}"
                "/profiles/${profile}/mods/Pragmicat Mix/${file}")
        endforeach()
    endforeach()
    foreach(file IN ITEMS mod.ini missions/missions.txt scripts/main.lua)
        pragmicat_embed_file(${target} "${project_root}/practice/${file}"
            "/profiles/practice/mods/Pragmicat Practice/${file}")
    endforeach()
endfunction()

function(pragmicat_embed_file target source destination)
    target_link_options(${target} PRIVATE "SHELL:--embed-file \"${source}@${destination}\"")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${source}")
endfunction()
