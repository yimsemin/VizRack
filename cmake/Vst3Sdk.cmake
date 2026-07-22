include(FetchContent)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "" FORCE)

set(_vizrack_local_vst3_sdk "${CMAKE_SOURCE_DIR}/external/vst3sdk")
if(NOT VIZRACK_VST3_SDK_PATH AND EXISTS "${_vizrack_local_vst3_sdk}/CMakeLists.txt")
    set(VIZRACK_VST3_SDK_PATH "${_vizrack_local_vst3_sdk}")
endif()

if(VIZRACK_VST3_SDK_PATH)
    get_filename_component(VIZRACK_VST3_SDK_ROOT "${VIZRACK_VST3_SDK_PATH}" ABSOLUTE)
    if(NOT EXISTS "${VIZRACK_VST3_SDK_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "VIZRACK_VST3_SDK_PATH is not a VST3 SDK checkout")
    endif()
    add_subdirectory("${VIZRACK_VST3_SDK_ROOT}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
else()
    if(NOT VIZRACK_FETCH_VST3_SDK)
        message(FATAL_ERROR "Set VIZRACK_VST3_SDK_PATH or enable VIZRACK_FETCH_VST3_SDK")
    endif()
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.0_build_66
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(vst3sdk)
    set(VIZRACK_VST3_SDK_ROOT "${vst3sdk_SOURCE_DIR}")
endif()
