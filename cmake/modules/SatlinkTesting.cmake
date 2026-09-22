# Unit-test frameworks: Unity for C, GoogleTest for C++.
# Offline builds: point FETCHCONTENT_SOURCE_DIR_UNITY / FETCHCONTENT_SOURCE_DIR_GOOGLETEST at
# local checkouts.
# @implements SRS-BLD-005

include(FetchContent)

FetchContent_Declare(unity
    URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM  # third-party headers must not trip our -Werror warning set
)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM  # third-party headers must not trip our -Werror warning set
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(unity googletest)

include(GoogleTest)

# satlink_add_unity_test(<name> SOURCES <files...> LIBS <targets...>)
function(satlink_add_unity_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE unity ${ARG_LIBS})
    satlink_target_defaults(${name})
    # Unity's setUp/tearDown have no prior prototype in the test file.
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -Wno-missing-prototypes)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# satlink_add_gtest(<name> SOURCES <files...> LIBS <targets...>)
function(satlink_add_gtest name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE GTest::gtest_main GTest::gmock ${ARG_LIBS})
    satlink_target_defaults(${name})
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()
