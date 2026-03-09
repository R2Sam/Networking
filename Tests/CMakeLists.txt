include(FetchContent)

# Fetch and build Catch2
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)

# This will make Catch2 available without adding to ALL
FetchContent_MakeAvailable(Catch2)

# Include directories for tests
target_include_directories(networking 
    INTERFACE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Create test executable
add_executable(networking_tests
    TestAsyncnetwork.cpp
)

# Link tests with networking library and Catch2
target_link_libraries(networking_tests
    PRIVATE
        networking  # Your existing library
        Catch2::Catch2WithMain
)

# Enable testing
enable_testing()
include(CTest)

# Register the test
add_test(NAME NetworkingTests COMMAND networking_tests)