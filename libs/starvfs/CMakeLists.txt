# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker

# The layered virtual filesystem (backlog workstream G; proposal §14.1,
# §12.5). docs/starvfs-api.md is the rationale for the API this target
# builds; this change is the "write the header and stub it" the backlog's
# "Rough size" section recommends ahead of the implementation itself.
#
# Independent of stardata and starcore -- workstream G's own preamble says
# so, and this library links neither: it knows nothing about `.star` files,
# reports its own Failure rather than a stardata::diag::Diagnostic, and has
# no spans. tests/unit/CMakeLists.txt's starvfs_unit_tests keeps that
# boundary visible in the build the same way stardata_unit_tests /
# starcore_unit_tests keeps the format/vocabulary boundary visible.
#
# No external dependency yet. miniz (vcpkg.json, chosen provisionally in
# CONTRIBUTING.md) is linked here starting at backlog G3, once zip_layer.cpp
# has an entry index to build with it -- linking it before anything calls
# it would pull a dependency into every consumer's build for no present
# benefit.
add_library(starvfs STATIC
    src/version.cpp
    src/error.cpp
    src/path.cpp
    src/layer.cpp
    src/native_host.cpp
    src/directory_layer.cpp
    src/zip_layer.cpp
    src/memory_layer.cpp
    src/vfs.cpp
)
add_library(starvfs::starvfs ALIAS starvfs)

target_include_directories(starvfs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(starvfs PUBLIC cxx_std_20)

starif_set_warnings(starvfs)
starif_enable_sanitizers(starvfs)
