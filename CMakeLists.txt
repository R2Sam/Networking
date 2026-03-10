cmake_minimum_required(VERSION 3.25)

project(Networking
  VERSION 0.0.1
  LANGUAGES CXX
)

include(FetchContent)

set(FETCHCONTENT_QUIET ON)

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
add_library(sodium STATIC IMPORTED)
set_target_properties(sodium PROPERTIES
  IMPORTED_LOCATION ${SODIUM_LIB}
)

FetchContent_Declare(
    ws
    GIT_REPOSITORY "https://github.com/Theldus/wsServer"
    GIT_TAG master
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(ws)

if (WIN32)
  FetchContent_Declare(
    z
    GIT_REPOSITORY "https://github.com/madler/zlib"
    GIT_TAG v1.3.2
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
    )

    FetchContent_MakeAvailable(z)
endif()

# ------------------------------------------------------------------------------
# Networking static library
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
  src/*.cpp
)

list(FILTER SRC EXCLUDE REGEX ".*/main\\.cpp$")

option(BUILD_TESTS_NETWORKING "Build tests" OFF)

add_library(networking_base INTERFACE)

target_include_directories(networking_base
  INTERFACE
    include
    src
)

target_link_libraries(networking_base
  INTERFACE
    ws
    sodium
    ${SYSTEM_LIBS}
)

if (WIN32)
  target_link_libraries(networking_base
    INTERFACE
    networking_base
    z)
endif()

if(NOT BUILD_TESTS_NETWORKING)

add_library(networking STATIC ${SRC})

target_link_libraries(networking
  PUBLIC
  networking_base)

endif()

# ------------------------------------------------------------------------------
# Networking tests
# ------------------------------------------------------------------------------

if(BUILD_TESTS_NETWORKING)
    enable_testing()

    FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

    file(GLOB_RECURSE TESTS CONFIGURE_DEPENDS
    tests/*.cpp
    )
    list(FILTER TESTS EXCLUDE REGEX ".*/main\\.cpp$")

    add_executable(testsNetworking
      ${SRC}
      ${TESTS})

    target_include_directories(testsNetworking
      PRIVATE
        include
        src
        tests
    )

    target_link_libraries(testsNetworking
      PRIVATE
        networking_base
        Catch2::Catch2WithMain
    )

    target_compile_definitions(testsNetworking PRIVATE TESTS=1)

    include(CTest)
    include(Catch)
    catch_discover_tests(testsNetworking)
endif()