function(pragmicat_add_recompiled_game_objects target upstream generated)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "Compile the Days browser game code with Emscripten")
    endif()
    get_filename_component(generated "${generated}" ABSOLUTE)
    include("${generated}/sources.cmake")
    if(NOT KHDAYS_ABI_PROFILE STREQUAL "wasm32" OR NOT KHDAYS_REWRITTEN)
        message(FATAL_ERROR "Generate the complete wasm32 source list before building Days")
    endif()
    if(NOT "${generated}/khdays_static_registry.c" IN_LIST KHDAYS_REWRITTEN)
        message(FATAL_ERROR "Generate the browser's static function and hook registries")
    endif()

    add_library(khdays-browser-game OBJECT ${KHDAYS_REWRITTEN}
        "${generated}/ds_functions.c" "${generated}/ds_names.c")
    target_include_directories(khdays-browser-game PRIVATE "${upstream}/port/compat" "${generated}")
    target_compile_features(khdays-browser-game PRIVATE c_std_17)
    # Keep the original four-byte layout for generated game structs. The host,
    # Lua and plugin code use the compiler's normal alignment.
    target_compile_options(khdays-browser-game PRIVATE
        -fms-extensions -fpack-struct=4 -fsigned-char)
    target_compile_definitions(khdays-browser-game PRIVATE
        KHDAYS_BROWSER=1 KHDAYS_BROWSER_MEMORY=1 KHDAYS_BROWSER_STATIC_REGISTRY=1)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:khdays-browser-game>)
endfunction()
