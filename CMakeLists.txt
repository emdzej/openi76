cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenI76 VERSION 0.0.1 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(I76_WARN -Wall -Wextra -Wno-unused-parameter -ffp-contract=off)   # no fused multiply-adds: the x87 rounds each op

# The portable core: everything except host backends (no platform headers).
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/*.c src/fmt/*.c src/net/*.c src/render/*.c src/shell/*.c)
set(HOST_SRC src/vfs_host.c src/platform_host.c)
foreach(f ${HOST_SRC} src/platform_gasm.c src/app.c)
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

# Platform: native (tools and headless tests) or gasm (openi76.wasm, a gasm guest):
#   cmake -S . -B build-gasm -DOPENI76_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
# (tools/fetch-gasm-sdk.sh fetches the released SDK and wasi-sdk into .deps/)
set(OPENI76_PLATFORM native CACHE STRING "Platform: native or gasm")
if(OPENI76_PLATFORM STREQUAL "gasm")
  include(Gasm)
  gasm_add_game(openi76 ${CORE_SRC} src/app.c src/platform_gasm.c)
  target_include_directories(openi76 PRIVATE src)
  target_compile_options(openi76 PRIVATE ${I76_WARN} -O2)
  target_link_options(openi76 PRIVATE -Wl,-z,stack-size=1048576 -Wl,--strip-debug)
  return()
endif()

add_library(i76_core STATIC ${CORE_SRC})
target_include_directories(i76_core PUBLIC src)
target_compile_options(i76_core PRIVATE ${I76_WARN})
if(NOT APPLE)
  target_link_libraries(i76_core PUBLIC m)   # libm is separate from libc on Linux
endif()

# Tests and tools read the game data through the file layer (./cd, or OPENI76_DATA=<dir|image>).
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*_test.c)
foreach(src ${TEST_SRC})
  get_filename_component(t ${src} NAME_WE)
  add_executable(${t} ${src} ${HOST_SRC})
  target_link_libraries(${t} PRIVATE i76_core)
  target_compile_options(${t} PRIVATE ${I76_WARN})
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# The core as a shared library, for the differential tests against the original's machine code
# (tools/emu/diff_*.py load it with ctypes): -DOPENI76_SHARED=ON.
option(OPENI76_SHARED "Build libi76core (shared) for tools/emu" OFF)
if(OPENI76_SHARED)
  add_library(i76core SHARED ${CORE_SRC} ${HOST_SRC})
  target_include_directories(i76core PRIVATE src)
  target_compile_options(i76core PRIVATE ${I76_WARN})
endif()
