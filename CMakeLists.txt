cmake_minimum_required(VERSION 3.25)
project(MyProject CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

include(FetchContent)

# -------------------------
# Platform detection
# -------------------------
if (WIN32)
    message(STATUS "Building on Windows")

    set(RELEASE_FLAGS -O3)
    set(DEBUG_FLAGS -Og -gdwarf-4)

    set(RELEASE_LINKER_FLAGS -s -mwindows)

    set(SYSTEM_LIBS opengl32 gdi32 winmm ws2_32 gomp -static)

    set(SODIUM_LIB ${LIB_DIR}/libsodium.a)

elseif(UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    message(STATUS "Building on Linux")

    set(RELEASE_FLAGS -O3)
    set(DEBUG_FLAGS -Og -gdwarf-4)

    set(RELEASE_LINKER_FLAGS -s)

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
set(COMMON_FLAGS
    -Wall
    -Wextra
    -pedantic
    -fopenmp
    -fmax-errors=5
    -Wno-missing-field-initializers
    -Wno-narrowing
    -Wno-enum-compare
    -Wno-reorder
    -Wno-shadow
    -Wno-deprecated-declarations
)

# Let VS Code / CMake Tools decide the build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose build type")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Building release")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building debug")
endif()

# -------------------------
# Imported libraries
# -------------------------
add_library(sodium STATIC IMPORTED GLOBAL)
set_target_properties(sodium PROPERTIES
    IMPORTED_LOCATION ${SODIUM_LIB}
    LINKER_LANGUAGE CXX
)

# -------------------------
# Fetch and build engine
# -------------------------
# FetchContent_Declare(
#     engine
#     GIT_REPOSITORY https://github.com/R2Sam/Engine.git
#     GIT_TAG main
# )

# FetchContent_MakeAvailable(engine)

# -------------------------
# Main executable
# -------------------------
file(GLOB_RECURSE MAIN_SRC CONFIGURE_DEPENDS src/*.cpp)
add_executable(main ${MAIN_SRC})

target_include_directories(main PUBLIC include src)

# Compile flags (per-config, VS Code friendly)
target_compile_options(main PRIVATE
    ${COMMON_FLAGS}
    $<$<CONFIG:Debug>:${DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${RELEASE_FLAGS}>
)

target_link_libraries(main PRIVATE ${SYSTEM_LIBS})
target_link_libraries(main PUBLIC sodium)

# -------------------------
# Release linker flags
# -------------------------
target_link_options(main PRIVATE
    $<$<CONFIG:Release>:${RELEASE_LINKER_FLAGS}>
)

# -------------------------
# Output directories
# -------------------------
set_target_properties(main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)