# Build the same Lua core and sandbox libraries as the native Days port.
function(pragmicat_add_browser_lua target)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "The Days browser Lua target requires Emscripten")
    endif()

    if(NOT TARGET khdays-browser-lua)
        include(FetchContent)
        FetchContent_Declare(pragmicat_lua
            URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
            URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR none)
        FetchContent_MakeAvailable(pragmicat_lua)
        file(GLOB lua_sources "${pragmicat_lua_SOURCE_DIR}/src/*.c")
        list(FILTER lua_sources EXCLUDE REGEX "/(lua|luac|liolib|ldblib|loadlib|linit)\\.c$")
        add_library(khdays-browser-lua STATIC ${lua_sources})
        target_include_directories(khdays-browser-lua PUBLIC "${pragmicat_lua_SOURCE_DIR}/src")
    endif()

    target_link_libraries(${target} PRIVATE khdays-browser-lua)
endfunction()
