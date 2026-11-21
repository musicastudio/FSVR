# fs1rLib          the engine, no Windows, no host, no GUI. This is what the plug-in links.
# fsvr_console     the test console (WinMM, Windows only), built into bin/.
# render_capture   the renderer behind the calibration loop, into bin/.
# wavetrace        a voice's operators sample by sample at several notes, into bin/.
# perfbench        the engine's cost on fixed scenarios, its output hashed, into bin/ (docs/performance.md).
# With FSVR_BUILD_PLUGIN, the plug-in: Hollow (hollow/) with the FSVR skin (plugin/), every format the
# platform has into bin/<format>. The first configure downloads the CLAP, VST3 and AU SDKs, RtAudio and
# RtMidi (through clap-wrapper) and the audio decoders.
#
# build.bat is the quick MSVC path; this file is for clang, CI, and the plug-in.
#   cmake -B build/cmake -S . && cmake --build build/cmake --config Release && ctest --test-dir build/cmake
#   cmake -B build/plugin -S . -DFSVR_BUILD_PLUGIN=ON && cmake --build build/plugin --config Release
cmake_minimum_required(VERSION 3.24)
# Before project(): C++17, the static C++ runtime, and on macOS universal binaries (arm64 and x86_64, so
# the same build runs on Apple silicon and Intel Macs; issue #3 was an arm64-only build) for 10.15 and up.
include(hollow/cmake/HollowDefaults.cmake)
project(fsvr VERSION 0.8.4 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()
# A multiply and an add stay two operations everywhere: the hosts, filters and effects are bit-exact only so, the
# tone generator's lane kernels replay its scalar arithmetic operation for operation, and clang on arm64 fuses by
# default. MSVC does not under /fp:precise, its default, which the engine's targets state anyway.
if(NOT MSVC)
  add_compile_options(-ffp-contract=off)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Everything meant to be run lands in bin/: the console here, the plug-in formats under bin/<Format>/.
set(FS1R_BIN ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# The two hosts and the filters and effects they drive. They are bit-exact only without FMA contraction, so they
# are a library of their own with its own flags; excess precision effect_host.h refuses at compile time.
# The effects and the voice filters (src/effects/, src/filters/) are bit-exact for the same reason, so they live here too.
file(GLOB VOP3HOST CONFIGURE_DEPENDS src/effects/host/*.cpp src/effects/*.cpp src/filters/*.cpp)
add_library(vop3host STATIC ${VOP3HOST} src/modulators/modulators.cpp src/fs1r/rom_tables.cpp src/kernel/dispatch.cpp)
target_include_directories(vop3host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(vop3host PROPERTIES CXX_STANDARD 20)
# The lane kernels (src/kernel/README.md), compiled once per instruction set the target can run: 0 scalar always,
# 1 sse2 and 2 avx2 on x86 (avx2 on 64 bits only), 3 neon on arm64; a universal macOS build takes all four, each
# slice keeping the ones it can run. dispatch.cpp picks among them at run time.
set(FSVR_DSP_KERNELS src/kernel/isa/bank_kernel.cpp src/kernel/isa/table.cpp)   # the DSP library's
set(FSVR_TG_KERNELS src/kernel/isa/group_kernel.cpp)                            # the engine's: it reaches its tables
set(FSVR_ISAS 0)
set(FSVR_HAVE_SSE2 0)
set(FSVR_HAVE_AVX2 0)
set(FSVR_HAVE_NEON 0)
if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
  set(FSVR_ISAS 0 1 2 3)
  set(FSVR_HAVE_SSE2 1)
  set(FSVR_HAVE_AVX2 1)
  set(FSVR_HAVE_NEON 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i386|i686|X86")
  list(APPEND FSVR_ISAS 1)
  set(FSVR_HAVE_SSE2 1)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND FSVR_ISAS 2)
    set(FSVR_HAVE_AVX2 1)
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
  list(APPEND FSVR_ISAS 3)
  set(FSVR_HAVE_NEON 1)
endif()
target_compile_definitions(vop3host PRIVATE FSVR_HAVE_SSE2=${FSVR_HAVE_SSE2} FSVR_HAVE_AVX2=${FSVR_HAVE_AVX2} FSVR_HAVE_NEON=${FSVR_HAVE_NEON})
function(fsvr_kernel_objects name isa)
  add_library(${name} OBJECT ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(${name} PRIVATE FSVR_ISA=${isa})
  set_target_properties(${name} PROPERTIES CXX_STANDARD 20)
  if(MSVC)
    target_compile_options(${name} PRIVATE /W3 /fp:precise /permissive-)
    if(isa EQUAL 2)
      target_compile_options(${name} PRIVATE /arch:AVX2)
    endif()
  else()
    target_compile_options(${name} PRIVATE -Wall -ffp-contract=off)
    if(isa EQUAL 2)
      if(APPLE)
        target_compile_options(${name} PRIVATE -Xarch_x86_64 -mavx2)
      else()
        target_compile_options(${name} PRIVATE -mavx2)
      endif()
    endif()
  endif()
endfunction()
foreach(isa IN LISTS FSVR_ISAS)
  fsvr_kernel_objects(fsvr_kernel_${isa} ${isa} ${FSVR_DSP_KERNELS})
  target_sources(vop3host PRIVATE $<TARGET_OBJECTS:fsvr_kernel_${isa}>)
endforeach()
foreach(t vop3host)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W3 /fp:precise /permissive-)
  else()
    target_compile_options(${t} PRIVATE -Wall -ffp-contract=off)
  endif()
endforeach()

add_library(fs1rLib STATIC
    src/tonegen/operator.cpp
    src/tonegen/channel.cpp
    src/engine/render.cpp
    src/engine/controllers.cpp
    src/engine/fseq.cpp
    src/engine/midi.cpp
    src/engine/notes.cpp
    src/presets/patch.cpp
    src/presets/rom.cpp
    src/device/device.cpp
    src/math/fastmath.cpp
    src/kernel/tonegen.cpp
    src/patchfiles/read.cpp
    src/patchfiles/dx7.cpp
    src/patchfiles/container_sysex.cpp
    src/patchfiles/container_smf.cpp
    src/patchfiles/container_fsvr.cpp
    src/patchfiles/container_minidexed.cpp
    src/patchfiles/container_headerless.cpp
    src/patchfiles/format_fs1r.cpp
    src/patchfiles/format_vced.cpp
    src/patchfiles/format_aced.cpp
    src/patchfiles/format_vmem.cpp
    src/patchfiles/format_amem.cpp
    src/patchfiles/format_dx7ii.cpp
    src/patchfiles/format_tx802.cpp
    src/patchfiles/performance.cpp
    src/selftest/selftest.cpp
    src/selftest/patchfiles.cpp)
target_include_directories(fs1rLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fs1rLib PUBLIC vop3host)
# The sample loop's maths backend, src/math/fastmath.h: LUT (the default, and the fastest measured), RAW
# (libm, kept for A/B renders), CORDIC, or HYBRID, where the table seeds a short CORDIC rotation.
set(FSVR_MATH "LUT" CACHE STRING "fastmath backend: RAW, LUT, CORDIC or HYBRID")
set_property(CACHE FSVR_MATH PROPERTY STRINGS RAW LUT CORDIC HYBRID)
set(_fsvr_math_names RAW LUT CORDIC HYBRID)
list(FIND _fsvr_math_names "${FSVR_MATH}" _fsvr_math_id)
if(_fsvr_math_id LESS 0)
  message(FATAL_ERROR "FSVR_MATH must be RAW, LUT, CORDIC or HYBRID, not ${FSVR_MATH}")
endif()
target_compile_definitions(fs1rLib PUBLIC FSVR_MATH=${_fsvr_math_id})
target_compile_definitions(fs1rLib PRIVATE FSVR_HAVE_SSE2=${FSVR_HAVE_SSE2} FSVR_HAVE_AVX2=${FSVR_HAVE_AVX2} FSVR_HAVE_NEON=${FSVR_HAVE_NEON})
foreach(isa IN LISTS FSVR_ISAS)
  fsvr_kernel_objects(fsvr_tg_kernel_${isa} ${isa} ${FSVR_TG_KERNELS})
  target_compile_definitions(fsvr_tg_kernel_${isa} PRIVATE FSVR_MATH=${_fsvr_math_id})
  target_sources(fs1rLib PRIVATE $<TARGET_OBJECTS:fsvr_tg_kernel_${isa}>)
endforeach()
if(MSVC)
  target_compile_options(fs1rLib PRIVATE /W3 /fp:precise)
else()
  target_compile_options(fs1rLib PRIVATE -Wall -Wno-unused-function)
endif()

# The renderer behind the calibration loop. The console does this too, but only on Windows, and
# tools/analyze_capture.py needs a render of every capture request to compare a recording against.
add_executable(render_capture tools/render_capture.cpp)
target_link_libraries(render_capture PRIVATE fs1rLib)
set_target_properties(render_capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${FS1R_BIN}>")
# A voice's operators sample by sample at several notes, as a table and a PNG (AGENTS.md, Tracing a waveform).
add_executable(wavetrace tools/wavetrace.cpp)
target_link_libraries(wavetrace PRIVATE fs1rLib)
set_target_properties(wavetrace PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${FS1R_BIN}>")
# The engine's cost on fixed scenarios and its output hashed, the record docs/performance.md is kept from.
add_executable(perfbench tools/perfbench.cpp)
target_link_libraries(perfbench PRIVATE fs1rLib)
set_target_properties(perfbench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${FS1R_BIN}>")

enable_testing()

if(WIN32)
  add_executable(fsvr_console src/console/main.cpp)
  target_link_libraries(fsvr_console PRIVATE fs1rLib winmm)
  set_target_properties(fsvr_console PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${FS1R_BIN}>")
  # The license and the notices beside it, for the console's release zip.
  add_custom_command(TARGET fsvr_console POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/LICENSE" "${FS1R_BIN}/FSVR-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/NOTICE.md" "${FS1R_BIN}/FSVR-NOTICE.md"
    VERBATIM)
  add_test(NAME engine COMMAND fsvr_console -selftest)
  add_test(NAME engine-sse2 COMMAND fsvr_console -selftest)
  set_tests_properties(engine-sse2 PROPERTIES ENVIRONMENT FSVR_SIMD=sse2)
else()
  message(STATUS "fsvr_console needs WinMM; building fs1rLib only")
  add_executable(engine_selftest tools/engine_selftest.cpp)
  target_link_libraries(engine_selftest PRIVATE fs1rLib)
  add_test(NAME engine COMMAND engine_selftest)
  if(FSVR_HAVE_SSE2)
    add_test(NAME engine-sse2 COMMAND engine_selftest)
    set_tests_properties(engine-sse2 PROPERTIES ENVIRONMENT FSVR_SIMD=sse2)
  endif()
endif()

# The filters' and effects' output, pinned (verified bit for bit against the firmware's DSP programs before
# those moved to the VOP3 project).
add_executable(dsp_check tools/dsp_check.cpp)
target_link_libraries(dsp_check PRIVATE vop3host)
if(NOT MSVC)
  target_compile_options(dsp_check PRIVATE -ffp-contract=off)
endif()
add_test(NAME dsp COMMAND dsp_check)
# The same on the sse2 kernels, and the avx2 object defines nothing outside its namespace (src/kernel/README.md).
if(FSVR_HAVE_SSE2)
  add_test(NAME dsp-sse2 COMMAND dsp_check)
  set_tests_properties(dsp-sse2 PROPERTIES ENVIRONMENT FSVR_SIMD=sse2)
endif()
find_package(Python3 COMPONENTS Interpreter)
if(FSVR_HAVE_AVX2 AND Python3_Interpreter_FOUND)
  add_test(NAME isa COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_isa.py avx2
           $<TARGET_OBJECTS:fsvr_kernel_2> $<TARGET_OBJECTS:fsvr_tg_kernel_2> COMMAND_EXPAND_LISTS)
endif()
# Their cost per sample, and the output's hash (not a test: a number to compare builds by).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  add_executable(dsp_bench EXCLUDE_FROM_ALL tools/dsp_bench.cpp)
  target_link_libraries(dsp_bench PRIVATE vop3host)
endif()

option(FSVR_BUILD_PLUGIN "Build the plug-in (downloads its SDKs on the first configure)" OFF)
if(FSVR_BUILD_PLUGIN)
  add_subdirectory(hollow)
  add_subdirectory(plugin)
endif()
