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
    zlib
    GIT_REPOSITORY "https://github.com/madler/zlib"
    GIT_TAG v1.3.2
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
    )

  set(ZLIB_BUILD_STATIC ON CACHE BOOL "Enable building zlib static library" FORCE)
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "Enable building zlib shared library" FORCE)

    FetchContent_MakeAvailable(zlib)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Utils")
    message(STATUS "Using local Utils repo")
    FetchContent_Declare(
        utils
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Utils/
    )
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../Utils")
  message(STATUS "Using local Utils repo")
    FetchContent_Declare(
        utils
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Utils/
    )
else()
    message(STATUS "Using online Utils repo")
    FetchContent_Declare(
        utils
        GIT_REPOSITORY https://github.com/R2Sam/Utils.git
        GIT_TAG main
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
endif()

FetchContent_MakeAvailable(utils)

# ------------------------------------------------------------------------------
# Networking static library
# ------------------------------------------------------------------------------

option(BUILD_TESTS_NETWORKING "Build tests" OFF)

if(NOT BUILD_TESTS_NETWORKING)

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

target_compile_options(networking PRIVATE
  ${COMMON_FLAGS}
  $<$<CONFIG:Debug>:
        -O0
        -g3
        -gdwarf-4
        -fno-inline
        -fno-omit-frame-pointer
        $<$<PLATFORM_ID:Linux>:
        -fsanitize=address
        -fsanitize=undefined>
    >
  )

target_link_options(networking PRIVATE
  $<$<CONFIG:Debug>:
        $<$<PLATFORM_ID:Linux>:
        -fsanitize=address
        -fsanitize=undefined>
    >
  )

target_link_libraries(networking
  PUBLIC
    ws
    sodium
    utils
  PRIVATE
    ${SYSTEM_LIBS}
)

if (WIN32)
  target_link_libraries(networking
    PUBLIC
    ZLIB::ZLIBSTATIC)
endif()

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

    file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp)
    list(FILTER SRC EXCLUDE REGEX ".*/main\\.cpp$")
    file(GLOB_RECURSE TESTS CONFIGURE_DEPENDS tests/*.cpp)

    add_executable(TestsNetworking
      ${SRC}
      ${TESTS})

    target_include_directories(TestsNetworking
      PRIVATE
        include
        src
        tests
    )

    target_compile_options(TestsNetworking PRIVATE
    ${COMMON_FLAGS}
    $<$<CONFIG:Debug>:
          $<$<PLATFORM_ID:Linux>:
          -fsanitize=address
          -fsanitize=undefined>
      >
    )

  target_link_options(TestsNetworking PRIVATE
    $<$<CONFIG:Debug>:
          $<$<PLATFORM_ID:Linux>:
          -fsanitize=address
          -fsanitize=undefined>
      >
    )

    target_link_libraries(TestsNetworking
      PRIVATE
        ws
        sodium
        utils
        Catch2::Catch2WithMain
        ${SYSTEM_LIBS}
    )

    set_target_properties(TestsNetworking PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

    target_compile_definitions(TestsNetworking PRIVATE TESTS=1)

    include(CTest)
    include(Catch)
    catch_discover_tests(TestsNetworking)
endif()