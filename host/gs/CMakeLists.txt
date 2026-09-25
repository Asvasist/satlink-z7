# SatLink-Z7 ground station (Qt 6 Widgets + Charts). Builds on its own, on Linux and Windows:
#
#   cmake -S host/gs -B build/gs -DCMAKE_PREFIX_PATH=<Qt>/lib/cmake
#   cmake --build build/gs
#
# The PUS library and the mission definitions are compiled from the main source tree, so the
# ground station always speaks the same protocol as the flight software.
# @implements SRS-GS-001
cmake_minimum_required(VERSION 3.21)
project(satlink_gs VERSION 0.6.0 DESCRIPTION "SatLink-Z7 ground station" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Charts)

set(SATLINK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_library(gs_core STATIC
    core/mission_tm.cpp
    ${SATLINK_ROOT}/libs/pus/src/space_packet.cpp
    ${SATLINK_ROOT}/libs/pus/src/tm_frame.cpp
    ${SATLINK_ROOT}/libs/common/src/crc16_ccitt.c
)
target_include_directories(gs_core PUBLIC
    core
    ${SATLINK_ROOT}/libs/pus/include
    ${SATLINK_ROOT}/libs/common/include
    ${SATLINK_ROOT}/linux/payload/include
)

qt_add_executable(satlink-gs WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/gs_link.cpp
    src/gs_link.hpp
    src/main_window.cpp
    src/main_window.hpp
    src/strip_chart.cpp
    src/strip_chart.hpp
    src/constellation_view.cpp
    src/constellation_view.hpp
)
target_link_libraries(satlink-gs PRIVATE gs_core Qt6::Widgets Qt6::Network Qt6::Charts)
target_compile_definitions(satlink-gs PRIVATE SATLINK_GS_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(gs_core PRIVATE /W4 /permissive-)
    target_compile_options(satlink-gs PRIVATE /W4 /permissive-)
else()
    target_compile_options(gs_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(satlink-gs PRIVATE -Wall -Wextra -Wpedantic)
endif()
option(SATLINK_GS_WERROR "Treat warnings as errors" OFF)
if(SATLINK_GS_WERROR)
    set_target_properties(gs_core satlink-gs PROPERTIES COMPILE_WARNING_AS_ERROR ON)
endif()

include(GNUInstallDirs)
install(TARGETS satlink-gs
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(WIN32)
    # windeployqt copies the Qt DLLs next to the executable in the install tree.
    qt_generate_deploy_app_script(TARGET satlink-gs OUTPUT_SCRIPT deploy_script
                                  NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${deploy_script})
endif()
