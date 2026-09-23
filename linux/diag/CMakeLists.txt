# satlink::diag - command logic of the satlink-diag tool, plus the executable on Linux targets.
# The logic takes its devices from a DeviceFactory, so host unit tests run it against mocks.
# @implements SRS-DIAG-001

add_library(satlink_diag STATIC
    src/diag.cpp
)
add_library(satlink::diag ALIAS satlink_diag)

target_include_directories(satlink_diag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_diag PUBLIC satlink::hal)
target_compile_features(satlink_diag PUBLIC cxx_std_20)
satlink_target_defaults(satlink_diag)

if(SATLINK_TARGET STREQUAL "linux")
    include(GNUInstallDirs)
    add_executable(satlink-diag src/main.cpp)
    target_link_libraries(satlink-diag PRIVATE satlink::diag)
    satlink_target_defaults(satlink-diag)
    install(TARGETS satlink-diag RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
