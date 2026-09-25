# satlink::gs_core - Qt-free part of the ground station (TC builders, TM decoders), built with
# the main project so the unit tests cover it. The standalone ground station build
# (host/gs/CMakeLists.txt) compiles the same sources.
# @implements SRS-GS-001
add_library(satlink_gs_core STATIC mission_tm.cpp)
add_library(satlink::gs_core ALIAS satlink_gs_core)
target_include_directories(satlink_gs_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/linux/payload/include
)
target_link_libraries(satlink_gs_core PUBLIC satlink::pus)
satlink_target_defaults(satlink_gs_core)
