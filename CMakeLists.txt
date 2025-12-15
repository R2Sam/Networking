cmake_minimum_required(VERSION 3.25)
project(MyProject CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

# -------------------------
# Platform detection
# -------------------------
if (WIN32)
    message(STATUS "Building on Windows")

    set(RELEASE_FLAGS "-O3")
    set(DEBUG_FLAGS "-Og -gdwarf-4")

    set(RELEASE_LINKER_FLAGS "-s -mwindows")

    set(SYSTEM_LIBS opengl32 gdi32 winmm ws2_32 winmm gomp -static)

    set(SODIUM_LIB ${LIB_DIR}/libsodium.a)

elseif(UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    message(STATUS "Building on Linux")

    set(RELEASE_FLAGS "-O3")
    set(DEBUG_FLAGS "-Og -gdwarf-4")

    set(RELEASE_LINKER_FLAGS "-s")

    set(SYSTEM_LIBS GL m pthread dl rt X11 gomp)

    set(SODIUM_LIB ${LIB_DIR}/libsodium-Linux.a)

elseif(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    message(FATAL_ERROR "Building on Linux arm not supported")

else()
    message(FATAL_ERROR "Unsupported OS")
endif()

# -------------------------
# Common flags
# -------------------------
set(COMMON_FLAGS "-Wall -Wextra -pedantic -fopenmp -fmax-errors=5 -Wno-missing-field-initializers -Wno-narrowing -Wno-enum-compare -Wno-reorder -Wno-shadow -Wno-deprecated-declarations")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Building release")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building debug")
endif()

set(CMAKE_CXX_FLAGS_DEBUG   "${COMMON_FLAGS} ${DEBUG_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${COMMON_FLAGS} ${RELEASE_FLAGS}")

add_library(sodium STATIC IMPORTED GLOBAL)
set_target_properties(sodium PROPERTIES IMPORTED_LOCATION ${SODIUM_LIB})

# Objects
file(GLOB_RECURSE SOURCES src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
add_library(objects OBJECT ${SOURCES})
target_include_directories(objects PUBLIC include src)

# Main executable
add_executable(main src/main.cpp $<TARGET_OBJECTS:objects>)
target_link_libraries(main PRIVATE ${SYSTEM_LIBS})
target_link_libraries(main PUBLIC sodium)
target_include_directories(main PUBLIC include src)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(main PROPERTIES LINK_FLAGS ${RELEASE_LINKER_FLAGS})
endif()

# Custom target for MakeTarget
add_custom_target(MAKE_TARGETS
    COMMAND ${CMAKE_COMMAND} -P "cmake/MakeTargets.cmake"
    VERBATIM
)

# Run target
if (UNIX AND NOT APPLE)
    add_custom_target(run
        COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/main
        DEPENDS main
        WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
elseif(WIN32)
    add_custom_target(run
        COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/main.exe
        DEPENDS main
        WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
endif()