include(FetchContent)

function(add_or_fetch_library)
    set(options)
    set(oneValueArgs NAME STATIC_LIB_FILE LOCAL_SRC_DIR GIT_URL GIT_TAG)
    set(multiValueArgs)
    cmake_parse_arguments(LIB "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT LIB_NAME)
        message(FATAL_ERROR "add_or_fetch_library requires NAME")
    endif()

    # 1. If static library exists (.a or .lib)
    if(LIB_STATIC_LIB_FILE AND EXISTS ${LIB_STATIC_LIB_FILE})
        message(STATUS "Using prebuilt static lib for ${LIB_NAME}: ${LIB_STATIC_LIB_FILE}")

        add_library(${LIB_NAME} STATIC IMPORTED GLOBAL)
        set_target_properties(${LIB_NAME} PROPERTIES
            IMPORTED_LOCATION ${LIB_STATIC_LIB_FILE}
        )
        list(APPEND CUSTOM_LIBS ${LIB_NAME})
        set(CUSTOM_LIBS "${CUSTOM_LIBS}" PARENT_SCOPE)
        set(CUSTOM_LIBS "${CUSTOM_LIBS}" CACHE INTERNAL "")
        return()
    endif()

    # 2. If LOCAL_SRC_DIR exists and has CMakeLists
    if(LIB_LOCAL_SRC_DIR AND EXISTS ${LIB_LOCAL_SRC_DIR}/CMakeLists.txt)
        message(STATUS "Using local source directory for ${LIB_NAME}: ${LIB_LOCAL_SRC_DIR}")
        add_subdirectory(${LIB_LOCAL_SRC_DIR})
        list(APPEND CUSTOM_LIBS ${LIB_NAME})
        set(CUSTOM_LIBS "${CUSTOM_LIBS}" PARENT_SCOPE)
        set(CUSTOM_LIBS "${CUSTOM_LIBS}" CACHE INTERNAL "")
        return()
    endif()

    # 3. Otherwise fetch from Git
    if(NOT LIB_GIT_URL)
        message(FATAL_ERROR "Missing STATIC_LIB_FILE, LOCAL_SRC_DIR, and GIT_URL for ${LIB_NAME}")
    endif()

    message(STATUS "Fetching ${LIB_NAME} from ${LIB_GIT_URL}")

    FetchContent_Declare(
        ${LIB_NAME}
        GIT_REPOSITORY ${LIB_GIT_URL}
        GIT_TAG ${LIB_GIT_TAG}
    )

    FetchContent_MakeAvailable(${LIB_NAME})

    list(APPEND CUSTOM_LIBS ${LIB_NAME})
    set(CUSTOM_LIBS "${CUSTOM_LIBS}" PARENT_SCOPE)
    set(CUSTOM_LIBS "${CUSTOM_LIBS}" CACHE INTERNAL "")
endfunction()