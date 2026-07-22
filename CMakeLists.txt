cmake_minimum_required(VERSION 4.2)

project(VizRack VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(VIZRACK_BUILD_TESTS "Build unit tests" ON)

# Declare the portable built-in contract before every Windows/VST dependency.
add_library(vizrack_builtin_core STATIC
    src/builtin/art_visualizer_engine.cpp
    src/builtin/art_visualizer_engine.h
    src/builtin/draw_list.cpp
    src/builtin/draw_list.h
    src/builtin/oscilloscope_engine.cpp
    src/builtin/oscilloscope_engine.h
)
target_include_directories(vizrack_builtin_core PUBLIC src)
if(MSVC)
    target_compile_options(vizrack_builtin_core PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(vizrack_builtin_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT WIN32)
    message(STATUS "Configuring the platform-neutral vizrack_builtin_core target only")
    return()
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "VizRack must be built for x64")
endif()

option(VIZRACK_FETCH_VST3_SDK "Fetch the pinned Steinberg VST3 SDK" ON)
set(VIZRACK_VST3_SDK_PATH "" CACHE PATH "Use an existing VST3 SDK checkout instead of FetchContent")

include(cmake/Vst3Sdk.cmake)

add_library(vizrack_core STATIC
    src/core/atomic_file.cpp
    src/core/atomic_file.h
    src/core/audio_ring.cpp
    src/core/audio_ring.h
    src/core/channel_mapper.cpp
    src/core/channel_mapper.h
    src/core/logger.cpp
    src/core/logger.h
    src/core/plugin_paths.cpp
    src/core/plugin_paths.h
    src/core/plugin_storage.cpp
    src/core/plugin_storage.h
    src/core/plugin_state.cpp
    src/core/plugin_state.h
    src/core/portable_paths.cpp
    src/core/portable_paths.h
    src/core/reconnect_policy.cpp
    src/core/reconnect_policy.h
    src/core/settings.cpp
    src/core/settings.h
    src/core/utf.cpp
    src/core/utf.h
    src/vst/plugin_catalog.cpp
    src/vst/plugin_catalog.h
)
target_include_directories(vizrack_core PUBLIC src)
target_compile_definitions(vizrack_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(vizrack_core PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(vizrack_core PUBLIC ole32 shell32)

add_executable(VizRack WIN32
    resources/app.rc
    resources/app.manifest
    src/app.cpp
    src/app.h
    src/main.cpp
    src/platform/audio_devices.cpp
    src/platform/audio_devices.h
    src/platform/wasapi_capture.cpp
    src/platform/wasapi_capture.h
    src/ui/art_visualizer_view.cpp
    src/ui/art_visualizer_view.h
    src/ui/gdi_back_buffer.cpp
    src/ui/gdi_back_buffer.h
    src/ui/gdi_draw_list_renderer.cpp
    src/ui/gdi_draw_list_renderer.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/oscilloscope_view.cpp
    src/ui/oscilloscope_view.h
    src/vst/plugin_discovery.cpp
    src/vst/plugin_discovery.h
    src/vst/vst_host.cpp
    src/vst/vst_host.h
    ${VIZRACK_VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
    ${VIZRACK_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp
    ${VIZRACK_VST3_SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp
)
target_include_directories(VizRack PRIVATE src resources ${VIZRACK_VST3_SDK_ROOT})
target_compile_definitions(VizRack PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    VIZRACK_VERSION="${PROJECT_VERSION}"
)
target_compile_options(VizRack PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:char8_t-)
target_link_libraries(VizRack PRIVATE
    vizrack_builtin_core
    vizrack_core
    sdk_hosting
    avrt
    comctl32
    dbghelp
    dwmapi
    gdi32
    gdiplus
    ole32
    propsys
    runtimeobject
    shell32
    shlwapi
    user32
    uuid
)
set_target_properties(VizRack PROPERTIES
    OUTPUT_NAME "VizRack"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:VizRack>"
)

install(TARGETS VizRack RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)

if(VIZRACK_BUILD_TESTS)
    enable_testing()
    add_executable(VizRackTests tests/test_main.cpp)
    target_link_libraries(VizRackTests PRIVATE vizrack_builtin_core vizrack_core)
    target_compile_definitions(VizRackTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(VizRackTests PRIVATE /W4 /permissive- /EHsc /utf-8)
    add_test(NAME VizRackTests COMMAND VizRackTests)
endif()
