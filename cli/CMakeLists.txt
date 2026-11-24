# Only the CLI needs compression support for .gdlvl files.
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 2c50092b3a4010760df02e7842960abdc3f25ee3
)
FetchContent_MakeAvailable(miniz)

CPMAddPackage("gh:prevter/slic@1.0.1")

add_library(pathfinder-level-io STATIC gdlvl.cpp)
target_include_directories(pathfinder-level-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pathfinder-level-io PUBLIC miniz)

add_executable(pathfinder-cli main.cpp)
target_link_libraries(pathfinder-cli PRIVATE pathfinder-level-io pathfinder-core slic)
