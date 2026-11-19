# Supply the native browser entry, core platform, rendering and plugin host.
# The caller supplies game objects, browser services and remaining modules.
function(pragmicat_add_browser_host target upstream generated)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "The Days browser host requires Emscripten")
    endif()
    if(NOT EXISTS "${generated}/khdays_data_area.h" OR
       NOT EXISTS "${generated}/sources.cmake")
        message(FATAL_ERROR "Generate the wasm32 Days source tree before configuring the browser host")
    endif()
    include("${generated}/sources.cmake")
    if(NOT KHDAYS_ABI_PROFILE STREQUAL "wasm32")
        message(FATAL_ERROR "The Days browser host must use a wasm32 generated source tree")
    endif()
    get_filename_component(native "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(port "${upstream}/port")
    set(mobiclip "${upstream}/external/khdays-decomp/libs/mobiclip/video/portable")
    file(STRINGS "${upstream}/VERSION" port_version LIMIT_COUNT 1)
    target_sources(${target} PRIVATE
        "${port}/runtime/core/start.c"
        "${port}/runtime/core/runtime.c"
        "${port}/runtime/core/diag.c"
        "${port}/runtime/core/boot.c"
        "${port}/runtime/core/io_regs.c"
        "${port}/runtime/core/language.c"
        "${port}/runtime/core/events.c"
        "${port}/runtime/core/files.c"
        "${port}/runtime/core/session_log.c"
        "${port}/runtime/core/window.c"
        "${port}/runtime/core/memory.c"
        "${port}/runtime/core/game_heap.c"
        "${port}/runtime/core/data_init.c"
        "${port}/runtime/core/overlays.c"
        "${port}/runtime/core/threads.c"
        "${port}/runtime/core/vram.c"
        "${port}/runtime/input/input.c"
        "${port}/runtime/input/controls_hooks.c"
        "${port}/runtime/input/analog.c"
        "${port}/runtime/input/sequencers.c"
        "${port}/runtime/input/walk.c"
        "${port}/runtime/plugins/plugins.c"
        "${port}/runtime/plugins/interfaces.c"
        "${port}/runtime/plugins/engine.c"
        "${port}/runtime/plugins/actors.c"
        "${port}/runtime/plugins/characters.c"
        "${port}/runtime/plugins/combat.c"
        "${port}/runtime/plugins/input.c"
        "${port}/runtime/plugins/parameters.c"
        "${port}/runtime/menus/camp.c"
        "${port}/runtime/menus/charselect.c"
        "${port}/runtime/menus/cinelist.c"
        "${port}/runtime/menus/daycard.c"
        "${port}/runtime/menus/enemy_viewer.c"
        "${port}/runtime/menus/newgame.c"
        "${port}/runtime/menus/shop.c"
        "${port}/runtime/display/display.c"
        "${port}/runtime/display/field_wipe.c"
        "${port}/runtime/display/gfx_check.c"
        "${port}/runtime/movies/video.c"
        "${mobiclip}/mobiclip_frame_core.cpp"
        "${mobiclip}/mobiclip_reference.cpp"
        "${native}/context/context_emscripten.c"
        "${native}/host/clock_emscripten.c"
        "${native}/host/input_emscripten.c"
        "${native}/host/instance_emscripten.c"
        "${native}/host/audio_emscripten.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/host_display_emscripten.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/mod_notices_emscripten.c"
        "${native}/renderer/frame_bridge.c"
        "${port}/gpu/gpu2d.c"
        "${port}/gpu/gpu3d.c"
        "${port}/gpu/texdecode.c"
        "${port}/gpu/vram_map.c")
    target_include_directories(${target} PRIVATE
        "${port}" "${port}/host/app" "${port}/runtime/core"
        "${upstream}/sdk/include" "${generated}" "${mobiclip}")
    target_compile_features(${target} PRIVATE c_std_17 cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        KHDAYS_BROWSER=1 KHDAYS_BROWSER_MEMORY=1 KHDAYS_BROWSER_STATIC_REGISTRY=1
        KHDAYS_VERSION="${port_version}")
    target_compile_options(${target} PRIVATE -fdeclspec)
    set_target_properties(${target} PROPERTIES SUFFIX ".mjs")
    target_link_options(${target} PRIVATE
        -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_ES6=1
        -sINVOKE_RUN=0 -sEXIT_RUNTIME=1
        "-sEXPORTED_RUNTIME_METHODS=['FS','HEAPU8','callMain']")
endfunction()
