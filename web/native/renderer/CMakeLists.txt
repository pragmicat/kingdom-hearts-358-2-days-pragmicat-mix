cmake_minimum_required(VERSION 3.20)
project(days_web_software_renderer C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(KHDAYS_PORT_SOURCE "" CACHE PATH "Yokimitsuro/khdays-port v0.1.5 source checkout")
if(NOT EXISTS "${KHDAYS_PORT_SOURCE}/port/gpu/gpu3d.c")
  message(FATAL_ERROR "Set KHDAYS_PORT_SOURCE to an upstream khdays-port v0.1.5 checkout")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../verify_upstream.py"
  --upstream "${KHDAYS_PORT_SOURCE}" COMMAND_ERROR_IS_FATAL ANY)

add_library(days_software_core STATIC
  "${KHDAYS_PORT_SOURCE}/port/gpu/gpu2d.c"
  "${KHDAYS_PORT_SOURCE}/port/gpu/gpu3d.c"
  "${KHDAYS_PORT_SOURCE}/port/gpu/texdecode.c"
  "${KHDAYS_PORT_SOURCE}/port/gpu/vram_map.c")
target_include_directories(days_software_core PUBLIC "${KHDAYS_PORT_SOURCE}/port")
target_compile_options(days_software_core PRIVATE -O2)

if(EMSCRIPTEN)
  add_executable(days_software_renderer frame_bridge.c)
  set_target_properties(days_software_renderer PROPERTIES SUFFIX ".mjs")
  target_link_libraries(days_software_renderer PRIVATE days_software_core m)
  target_link_options(days_software_renderer PRIVATE
    --no-entry
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=web,worker,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFILESYSTEM=0"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_days_web_frame_rgba','_days_web_frame_generation','_days_web_present','_days_web_render','_days_web_reset_renderer']")
else()
  enable_testing()
  add_executable(days_renderer_test tests/renderer_test.c frame_bridge.c)
  target_link_libraries(days_renderer_test PRIVATE days_software_core m)
  target_include_directories(days_renderer_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME days_renderer COMMAND days_renderer_test)
endif()
