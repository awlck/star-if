# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
#
# backlog B4: warnings are errors, everywhere, on every target. One function
# so the flags live in exactly one place rather than being copy-pasted into
# every library's CMakeLists.txt.

function(starif_set_warnings target)
    if (MSVC)
        # /utf-8 is not a warning flag, but it belongs with them: the sources
        # are UTF-8 (spec section markers appear in diagnostic messages, and
        # `--` and `—` in comments), and MSVC otherwise reads a BOM-less file
        # in the machine's active code page and re-encodes narrow literals
        # into it. That makes a diagnostic's bytes depend on the code page of
        # whoever built the compiler's host, which is exactly the kind of
        # difference the round-trip requirement of spec §14.2 cannot tolerate.
        # GCC and Clang already default to UTF-8 for both charsets.
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()
