// WLM2PST - streaming SHA-256, used for source-file integrity checks and
// error-report fingerprints (spec section 21: SHA-256 is an allowed log
// field even though message content is not).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "common/errors/result.h"

namespace wlm2pst {

// Streaming SHA-256 hasher. Non-copyable. Implemented with Windows CNG
// (sha256_win.cpp) on Windows and a clean-room portable implementation
// (sha256_portable.cpp) elsewhere, hidden behind a pimpl so this header
// never depends on either platform's crypto headers.
class Sha256 {
public:
    Sha256();
    ~Sha256();

    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void update(const void* data, size_t len);
    std::array<uint8_t, 32> finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Lowercase hex encoding of a digest (or any byte buffer).
std::string to_hex_lower(const uint8_t* data, size_t len);

// Hashes the file at `path`, streaming it in 256 KiB chunks so the whole
// file is never loaded into memory. Opens the file read-only, denying
// writes for the duration of the read (spec section 7/24: source files are
// never modified and are protected from concurrent writers while read).
Result<std::array<uint8_t, 32>> sha256_file(const std::wstring& path);

}  // namespace wlm2pst
