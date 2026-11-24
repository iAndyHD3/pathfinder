# Standalone pathfinding core: no Geode / cocos dependencies.
include(FetchContent)

set(LIBGDR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libGDR
    GIT_REPOSITORY https://github.com/maxnut/GDReplayFormat.git
    GIT_TAG gdr2
)
FetchContent_MakeAvailable(libGDR)

add_library(pathfinder-core STATIC pathfinder.cpp difficulty.cpp robustness.cpp)
target_include_directories(pathfinder-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathfinder-core PUBLIC gd-sim libGDR)

if(MSVC)
    target_compile_options(pathfinder-core PRIVATE /O2)
else()
    target_compile_options(pathfinder-core PRIVATE -O3 -fno-math-errno -fno-trapping-math)
endif()
