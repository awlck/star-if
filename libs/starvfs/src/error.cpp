// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/error.hpp"

namespace starvfs {

std::string_view to_string(Error error) noexcept {
    switch (error) {
    case Error::NotFound:
        return "NotFound";
    case Error::NotAFile:
        return "NotAFile";
    case Error::NotADirectory:
        return "NotADirectory";
    case Error::ReadOnlyLayer:
        return "ReadOnlyLayer";
    case Error::InvalidPath:
        return "InvalidPath";
    case Error::AlreadyExists:
        return "AlreadyExists";
    case Error::OutOfRange:
        return "OutOfRange";
    case Error::ArchiveCorrupt:
        return "ArchiveCorrupt";
    case Error::UnsupportedCompression:
        return "UnsupportedCompression";
    case Error::HostFailure:
        return "HostFailure";
    case Error::NotImplemented:
        return "NotImplemented";
    }
    // Deliberately no default case: -Wall/-Wextra + -Werror (backlog B4)
    // turns an Error value nothing here handles into a build failure at
    // the switch itself, not a silent "Unknown" at run time.
    return "Unknown";
}

} // namespace starvfs
