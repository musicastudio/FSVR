# The FSVR plug-in: fs1rLib behind the Hollow GUI (hollow/), whose skin is skin/. Every format lands in
# bin/<format> (hollow_add_plugin's BIN). The processor never models synthesis; see plugin.cpp.
include(FetchContent)
# Import Audio's decoders, both public domain: dr_wav (WAV, AIFF) and dr_mp3, and stb_vorbis.
FetchContent_Declare(dr_libs URL https://github.com/mackron/dr_libs/archive/dfe8377631000664666519fdb83da193fd8037f4.tar.gz)
FetchContent_Declare(stb URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz)
FetchContent_MakeAvailable(dr_libs stb)

# The factory banks: 1408 voices, 384 performances and 90 Fseqs, packed out of presets/ by
# tools/make_presets_blob.py (generated/README.md).
hollow_embed_files("${CMAKE_CURRENT_BINARY_DIR}/factory.cpp" kFactoryFiles kFactoryFileCount FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/fs1r_presets.syx"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/fs1r_performances.syx"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/fs1r_fseqs.syx"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/contributors.txt")

hollow_add_plugin(fsvr
  NAME "FSVR"
  SKIN "${CMAKE_CURRENT_SOURCE_DIR}/skin"
  SOURCES plugin.cpp library.cpp audio_decode.cpp audio_fseq.cpp "${CMAKE_CURRENT_BINARY_DIR}/factory.cpp"
  BUNDLE_ID studio.musica.fsvr
  VERSION ${PROJECT_VERSION}
  VENDOR musica.studio
  AU_MANUFACTURER Msst
  AU_SUBTYPE Fsvr
  WINDOWS_ICON "${PROJECT_SOURCE_DIR}/icon.ico"
  MACOS_ICON "${PROJECT_SOURCE_DIR}/icon.icns"
  BIN "${FS1R_BIN}")

target_link_libraries(fsvr_lib PRIVATE fs1rLib)
target_compile_definitions(fsvr_lib PRIVATE FSVR_VERSION="${PROJECT_VERSION}")
target_include_directories(fsvr_lib PRIVATE "${dr_libs_SOURCE_DIR}" "${stb_SOURCE_DIR}")
if(WIN32)
  target_link_libraries(fsvr_lib PRIVATE mfplat mfreadwrite mfuuid ole32 shell32)
elseif(APPLE)
  target_link_libraries(fsvr_lib PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
endif()

# The processor's own check: banks, the sysex round trip, morph, Import Audio (tools/check_plugin.cpp).
add_executable(check_plugin "${PROJECT_SOURCE_DIR}/tools/check_plugin.cpp")
target_link_libraries(check_plugin PRIVATE fsvr_lib)
target_include_directories(check_plugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(check_plugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")
add_test(NAME plugin COMMAND check_plugin)

# The editor's own check: the skin and the processor driven through a windowless Gui (tools/check_gui.cpp).
add_executable(check_gui "${PROJECT_SOURCE_DIR}/tools/check_gui.cpp")
target_link_libraries(check_gui PRIVATE fsvr_lib)
target_include_directories(check_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(check_gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")
add_test(NAME gui COMMAND check_gui)
