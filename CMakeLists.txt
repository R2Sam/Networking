cmake_minimum_required(VERSION 3.25)
project(MyProject CXX)

# -------------------------
# C++ standard & output
# -------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# -------------------------
# Platform detection
# -------------------------
if (WIN32)
    message(STATUS "Building on Windows")

    set(COMMON_FLAGS "-Wall -Wextra -pedantic -fopenmp -fmax-errors=5 -Wno-missing-field-initializers -Wno-narrowing -Wno-enum-compare -Wno-reorder -Wno-shadow -Wno-deprecated-declarations -O3")
    set(LINK_FLAGS "-s -mwindows")

    set(SYSTEM_LIBS opengl32 gdi32 winmm ws2_32 gomp z -static)

    set(SODIUM_LIB ${LIB_DIR}/libsodium.a)

elseif(UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    message(STATUS "Building on Linux")

    set(COMMON_FLAGS "-Wall -Wextra -pedantic -fopenmp -fmax-errors=5 -Wno-missing-field-initializers -Wno-narrowing -Wno-enum-compare -Wno-reorder -Wno-shadow -Wno-deprecated-declarations -O3")
    set(LINK_FLAGS "-s")

    set(SYSTEM_LIBS GL m pthread dl rt X11 gomp z)

    set(SODIUM_LIB ${LIB_DIR}/libsodium-Linux.a)

elseif(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    message(FATAL_ERROR "Building on Linux arm not supported")

else()
    message(FATAL_ERROR "Unsupported OS")
endif()

# -------------------------
# Compiler flags
# -------------------------
set(CMAKE_CXX_FLAGS "${COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${LINK_FLAGS}")

# -------------------------
# Imported libraries (release only)
# -------------------------
add_library(Networking::sodium STATIC IMPORTED)
set_target_properties(Networking::sodium PROPERTIES IMPORTED_LOCATION ${SODIUM_LIB})
# -------------------------
# Object library for sources
# -------------------------
file(GLOB_RECURSE SOURCES src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(NetworkingObjects OBJECT ${SOURCES})
target_include_directories(NetworkingObjects PUBLIC include src)

# -------------------------
# Static library for engine
# -------------------------
add_library(networking STATIC $<TARGET_OBJECTS:NetworkingObjects>)
target_include_directories(networking PUBLIC include src)
target_link_libraries(networking PRIVATE Networking::sodium ${SYSTEM_LIBS})