# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
#
# backlog B4: warnings are errors, everywhere, on every target. One function
# so the flags live in exactly one place rather than being copy-pasted into
# every library's CMakeLists.txt.

function(starif_set_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()
