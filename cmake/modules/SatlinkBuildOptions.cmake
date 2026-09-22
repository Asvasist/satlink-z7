# Warnings, sanitizers, coverage and clang-tidy for SatLink targets.
# Third-party code (Unity, GoogleTest) is deliberately left untouched.
# @implements SRS-BLD-002
# @implements SRS-BLD-004

set(SATLINK_C_WARNINGS
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-align
    -Wundef -Wdouble-promotion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes
)
set(SATLINK_CXX_WARNINGS
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-align
    -Wundef -Wdouble-promotion -Wformat=2 -Wold-style-cast -Wnon-virtual-dtor
    -Woverloaded-virtual
)

if(SATLINK_ENABLE_CLANG_TIDY)
    find_program(SATLINK_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

# Apply the project defaults to a SatLink library or executable.
function(satlink_target_defaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${SATLINK_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${SATLINK_C_WARNINGS}>
            $<$<COMPILE_LANGUAGE:CXX>:${SATLINK_CXX_WARNINGS}>
            $<$<BOOL:${SATLINK_WARNINGS_AS_ERRORS}>:-Werror>
        )
    endif()

    if(SATLINK_TARGET MATCHES "^(rtos|hkc)$")
        # Bare-metal targets: keep sections separate so the linker can drop unused code.
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    endif()

    if(SATLINK_TARGET STREQUAL "host")
        if(SATLINK_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
        if(SATLINK_ENABLE_COVERAGE)
            target_compile_options(${target} PRIVATE --coverage -O0 -g)
            target_link_options(${target} PRIVATE --coverage)
        endif()
    endif()

    if(SATLINK_ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES
            C_CLANG_TIDY "${SATLINK_CLANG_TIDY_EXE}"
            CXX_CLANG_TIDY "${SATLINK_CLANG_TIDY_EXE}")
    endif()
endfunction()
