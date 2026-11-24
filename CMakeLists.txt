cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(pathfinder VERSION 1.0.0 LANGUAGES C CXX)

option(PATHFINDER_BUILD_MOD "Build the Geode mod" OFF)
option(PATHFINDER_BUILD_CLI "Build the standalone CLI" ON)

# Geode brings its own CPM; CLI-only builds need the vendored copy.
if (NOT PATHFINDER_BUILD_MOD)
    set(EXTRACTED_CPM_VERSION 0.43.2)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)
endif()

# Shared, game-independent bits.
add_subdirectory(gd-sim)
add_subdirectory(core)

# The in-game UI layer. Nothing here is needed by the CLI.
if (PATHFINDER_BUILD_MOD)
    if (NOT DEFINED ENV{GEODE_SDK})
        message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
    else()
        message(STATUS "Found Geode: $ENV{GEODE_SDK}")
    endif()

    add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
    CPMAddPackage("gh:camila314/UIBuilder#main")

    add_library(${PROJECT_NAME} SHARED
        src/main.cpp
        src/checker.cpp
        src/debug.cpp
    )

    # add custom target to strip binary
    if (APPLE)
    add_custom_target(strip ALL
        COMMAND $<$<CONFIG:Debug>:echo> strip -x $<TARGET_FILE:${PROJECT_NAME}>
        DEPENDS ${PROJECT_NAME}
    )
    endif()

    setup_geode_mod(${PROJECT_NAME})
    target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-DDEBUG_MODE=1>)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-g> -O3 -fno-math-errno -fno-trapping-math -fvisibility=hidden -fomit-frame-pointer)
    target_link_libraries(${PROJECT_NAME} gd-sim UIBuilder libGDR pathfinder-core)
endif()

if (PATHFINDER_BUILD_CLI)
    add_subdirectory(cli)
endif()
