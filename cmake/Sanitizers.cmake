# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
#
# backlog B4: ASan + UBSan in Debug on Linux and macOS. MSVC has its own,
# incompatible sanitizer story and is deliberately excluded rather than
# half-supported.
#
# CMAKE_BUILD_TYPE is what single-config generators (Ninja, Unix Makefiles)
# use, which is what the linux-gcc/linux-clang/macos-clang presets are built
# on (CMakePresets.json) -- this does not need to handle multi-config
# generators, because MSVC (the one multi-config target here) is excluded
# above anyway.

option(STARIF_ENABLE_SANITIZERS
    "Enable AddressSanitizer + UndefinedBehaviorSanitizer in Debug builds (Linux/macOS only)"
    ON
)

function(starif_enable_sanitizers target)
    if (NOT STARIF_ENABLE_SANITIZERS)
        return()
    endif()
    if (MSVC)
        return()
    endif()
    if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        return()
    endif()

    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()
