# satlink-modem-perf: link performance sweep (docs/performance). Host only.
# @implements SRS-PERF-001
add_executable(satlink-modem-perf modem_perf.cpp)
target_link_libraries(satlink-modem-perf PRIVATE satlink::modem)
satlink_target_defaults(satlink-modem-perf)
