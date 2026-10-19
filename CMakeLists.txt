cmake_minimum_required(VERSION 3.15)

project(byte
  VERSION 0.0.1
  LANGUAGES C
)

# Debug tracing (disassembly + VM trace). Off by default so program output
# stays clean; enable with -DBYTE_DEBUG_TRACE=ON when debugging the compiler/VM.
option(BYTE_DEBUG_TRACE "Print compiler/VM debug trace during execution" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SOURCES src/*.c)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  src
  src/compiler
  src/core
  src/debug
  src/utils
)

target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall
  -Wextra
  -fPIC
)

target_link_libraries(${PROJECT_NAME} PRIVATE m)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:Debug>:DEBUG>
  $<$<BOOL:${BYTE_DEBUG_TRACE}>:BYTE_DEBUG_TRACE>
  VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  VERSION_MINOR=${PROJECT_VERSION_MINOR}
  VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  DEBUG_POSTFIX ""
)

add_custom_target(run
  COMMAND $<TARGET_FILE:${PROJECT_NAME}>
  DEPENDS ${PROJECT_NAME}
)

# --- Tests -------------------------------------------------------------

enable_testing()

# The language test suite lives in tests/ as .byte programs verified against
# snapshots. Run via `ctest --test-dir build` or `./tests/run.sh` directly.
add_test(
  NAME language
  COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run.sh
          --bin $<TARGET_FILE:byte>
)
