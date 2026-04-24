cmake_minimum_required(VERSION 3.25)

project(
	Networking
	VERSION 0.0.1
	LANGUAGES CXX C
)

# -------------------------
# Global configuration
# -------------------------
include(FetchContent)

set(FETCHCONTENT_QUIET ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

option(BUILD_TESTING "Build tests" OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Detect Emscripten
set(IS_EMSCRIPTEN OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
	set(IS_EMSCRIPTEN ON)
endif()

# -------------------------
# Options
# -------------------------
add_library(Networking_options INTERFACE)
add_library(Networking::options ALIAS Networking_options)

target_compile_features(
	Networking_options
	INTERFACE
		cxx_std_23
		c_std_23
)

# -------------------------
# Safety
# -------------------------
add_library(Networking_safety INTERFACE)
add_library(Networking::safety ALIAS Networking_safety)

target_compile_options(
	Networking_safety
	INTERFACE
		-fstack-protector-strong
		-D_GLIBCXX_ASSERTIONS

		$<$<CONFIG:Debug>:
			-O0
			-g3
			-gdwarf-4
			-fno-inline
			-fno-omit-frame-pointer

			$<$<AND:$<PLATFORM_ID:Linux>,$<NOT:$<BOOL:${IS_EMSCRIPTEN}>>>:
                -fsanitize=address
                -fsanitize=undefined
            >
		>

		$<$<CONFIG:Release>:
            -O3

            $<$<AND:$<PLATFORM_ID:Linux>,$<NOT:$<BOOL:${IS_EMSCRIPTEN}>>>:
                -fsanitize-trap=undefined
            >
        >
)

target_link_options(
	Networking_safety
	INTERFACE
		$<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>,$<NOT:$<BOOL:${IS_EMSCRIPTEN}>>>:
			-fsanitize=address
			-fsanitize=undefined
		>

		$<$<AND:$<CONFIG:Release>,$<PLATFORM_ID:Linux>,$<NOT:$<BOOL:${IS_EMSCRIPTEN}>>>:
		>
)

# -------------------------
# Platform
# -------------------------
add_library(Networking_platform INTERFACE)
add_library(Networking::platform ALIAS Networking_platform)

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

target_link_libraries(
	Networking_platform
	INTERFACE
		$<$<PLATFORM_ID:Windows>:
			ZLIB::ZLIBSTATIC
			winmm
		>
)

target_link_options(
	Networking_platform
	INTERFACE
		$<$<PLATFORM_ID:Windows>:
			-static
		>
)

target_link_libraries(
	Networking_platform
	INTERFACE
		$<$<AND:$<PLATFORM_ID:Linux>,$<NOT:$<BOOL:${IS_EMSCRIPTEN}>>>:
			pthread
			z
		>
)

# -------------------------
# Libs
# -------------------------

add_library(sodium STATIC IMPORTED)
set_target_properties(sodium PROPERTIES
	IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libsodium-Linux.a
)

if (WIN32)
	
	set_target_properties(sodium PROPERTIES
		IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libsodium.a
	)

elseif (${IS_EMSCRIPTEN})

	set_target_properties(sodium PROPERTIES
		IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libsodium-Web.a
	)

endif()

# -------------------------
# Dependencies
# -------------------------

FetchContent_Declare(
	ws
	GIT_REPOSITORY "https://github.com/Theldus/wsServer"
	GIT_TAG master
	GIT_SHALLOW TRUE
	EXCLUDE_FROM_ALL
)

if(EXISTS "/home/sam/Documents/Projects/Utils")
	message(STATUS "Using local Utils repo")

	FetchContent_Declare(
		utils
		SOURCE_DIR /home/sam/Documents/Projects/Utils/
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

FetchContent_MakeAvailable(utils ws)

# -------------------------
# Networking static library
# -------------------------
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp)
list(FILTER SRC EXCLUDE REGEX ".*/main\\.cpp$")

add_library(networking STATIC ${SRC})

target_include_directories(
	networking
	PUBLIC
		include
		src
)

target_compile_options(
	networking
	PRIVATE
		-Wall
		-Wextra
		-pedantic
		-fmax-errors=5

		-Wno-missing-field-initializers
		-Wno-narrowing
		-Wno-enum-compare
		-Wno-reorder
		-Wno-shadow
		-Wno-deprecated-declarations
)

target_link_libraries(
	networking
	PUBLIC
		utils
		
	PRIVATE
		Networking::options
		Networking::safety
		Networking::platform

		ws
		sodium
)

# -------------------------
# Emscripten
# -------------------------
target_compile_options(
	networking
	PRIVATE
		$<$<BOOL:${IS_EMSCRIPTEN}>:
			-pthread
			-sUSE_PTHREADS
		>
)

target_link_options(
	networking
	PRIVATE
		$<$<BOOL:${IS_EMSCRIPTEN}>:
			-pthread
			-sUSE_PTHREADS
			-sASYNCIFY
			-sASSERTIONS
			-sPROXY_TO_PTHREAD
		>
)

# -------------------------
# Tests
# -------------------------
if(BUILD_TESTING)
    include(CTest)
    
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(Catch2)

    file(GLOB_RECURSE TEST_SRC CONFIGURE_DEPENDS src/*.cpp)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/main\\.cpp$")
    file(GLOB_RECURSE TESTS CONFIGURE_DEPENDS tests/*.cpp)

    add_executable(
        TestsNetworking
        ${TEST_SRC}
        ${TESTS}
    )

    target_include_directories(
        TestsNetworking
        PRIVATE
            include
            src
            tests
    )

    target_compile_options(
		TestsNetworking
		PRIVATE
			-Wall
			-Wextra
			-pedantic
			-fmax-errors=5

			-Wno-missing-field-initializers
			-Wno-narrowing
			-Wno-enum-compare
			-Wno-reorder
			-Wno-shadow
			-Wno-deprecated-declarations
	)

    target_link_libraries(
        TestsNetworking
        PRIVATE
            Networking::options
            Networking::safety
            Networking::platform

            utils
            ws
            sodium

            Catch2::Catch2WithMain
    )

    set_target_properties(
        TestsNetworking
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )

    target_compile_definitions(TestsNetworking PRIVATE TESTS=1)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(TestsNetworking)
endif()