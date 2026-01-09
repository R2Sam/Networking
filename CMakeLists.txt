cmake_minimum_required(VERSION 3.25)

project(Networking
  VERSION 1.0.0
  LANGUAGES CXX
)

# ------------------------------------------------------------------------------
# C++ standard & output
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# ------------------------------------------------------------------------------
# Platform detection
# ------------------------------------------------------------------------------
if (WIN32)
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
    -O3
  )

  set(LINK_FLAGS
    -s
    -mwindows
  )

  set(SYSTEM_LIBS
    opengl32
    gdi32
    winmm
    ws2_32
    gomp
    ${LIB_DIR}/libz.a
    -static
  )

  set(SODIUM_LIB ${LIB_DIR}/libsodium.a)

elseif (UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
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
    -O3
  )

  set(LINK_FLAGS
    -s
  )

  set(SYSTEM_LIBS
    GL
    m
    pthread
    dl
    rt
    X11
    gomp
    z
  )

  set(SODIUM_LIB ${LIB_DIR}/libsodium-Linux.a)

elseif (UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
  message(FATAL_ERROR "Linux ARM is not supported")

else()
  message(FATAL_ERROR "Unsupported platform")
endif()

# ------------------------------------------------------------------------------
# Global compiler & linker flags
# ------------------------------------------------------------------------------
string(JOIN " " CMAKE_CXX_FLAGS ${COMMON_FLAGS})
string(JOIN " " CMAKE_EXE_LINKER_FLAGS ${LINK_FLAGS})

# ------------------------------------------------------------------------------
# Imported third-party libraries
# ------------------------------------------------------------------------------
add_library(Networking::sodium STATIC IMPORTED)
set_target_properties(Networking::sodium PROPERTIES
  IMPORTED_LOCATION ${SODIUM_LIB}
)

# ------------------------------------------------------------------------------
# Networking static library
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
  src/*.cpp
)

list(FILTER SRC EXCLUDE REGEX ".*/main\\.cpp$")

add_library(networking STATIC ${SRC})

target_include_directories(networking
  PUBLIC
    include
    src
)

target_link_libraries(networking
  PRIVATE
    Networking::sodium
    ${SYSTEM_LIBS}
)
