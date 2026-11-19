cmake_minimum_required(VERSION 3.20)
project(pragmicat_browser_mod_smoke LANGUAGES C CXX)
set(KHDAYS_PORT "" CACHE PATH "Days PC port v0.1.5 source checkout")
if(NOT EXISTS "${KHDAYS_PORT}/port/host/app/files.h")
    message(FATAL_ERROR "Set KHDAYS_PORT to the Days PC port v0.1.5 checkout")
endif()
include(../PragmicatBrowser.cmake)
add_executable(pragmicat_mod_smoke smoke.c)
target_include_directories(pragmicat_mod_smoke PRIVATE "${KHDAYS_PORT}/port")
pragmicat_add_browser_mod(pragmicat_mod_smoke)
set_target_properties(pragmicat_mod_smoke PROPERTIES SUFFIX ".mjs")
target_link_options(pragmicat_mod_smoke PRIVATE
    --no-entry -sENVIRONMENT=node -sMODULARIZE=1 -sEXPORT_ES6=1
    -sASYNCIFY -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1
    "-sEXPORTED_RUNTIME_METHODS=['FS','ccall']")
