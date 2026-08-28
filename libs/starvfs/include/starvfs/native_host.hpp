// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>

#include "starvfs/host.hpp"

namespace starvfs {

// The Phase 0 HostIo: real files under a real root directory. This is the
// one type in the library that is meant to eventually reach for
// std::filesystem -- see host.hpp for why nothing else in starvfs may.
//
// Stubbed for this change (backlog G1: write the API's shape before its
// implementation). Every method fails with Error::NotImplemented; G2 fills
// these in for real, including the case-sensitivity defence
// docs/starvfs-api.md describes for DirectoryLayer built on top of this.
class NativeHostIo final : public HostIo {
public:
    explicit NativeHostIo(std::string root_directory);

    Future<Bytes> read(std::string_view host_path) override;
    Future<Bytes> read_range(std::string_view host_path, std::uint64_t offset,
                             std::uint64_t length) override;
    Future<void> write(std::string_view host_path, Bytes contents) override;
    Future<void> remove(std::string_view host_path) override;
    Future<void> make_directory(std::string_view host_path) override;
    Future<HostStat> stat(std::string_view host_path) override;
    Future<std::vector<HostEntry>> list(std::string_view host_path) override;

    [[nodiscard]] CaseSensitivity case_sensitivity() const noexcept override;

private:
    std::string root_directory_;
};

} // namespace starvfs
