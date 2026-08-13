// Clean-room portable SHA-256 (FIPS 180-4) and a std::ifstream-based
// sha256_file(), used off-Windows (native Linux/macOS builds, unit tests)
// and as the implementation checked by non-Windows CI.
//
// This whole file is guarded out on Windows: the build's source glob only
// excludes `_win.cpp` files on non-Windows platforms, so on Windows this
// unsuffixed-looking-but-actually-suffixed-differently file would otherwise
// be compiled too and double-define Sha256::Impl alongside sha256_win.cpp.
#ifndef _WIN32

#include "common/hashing/sha256.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "common/unicode/utf.h"

namespace wlm2pst {

namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

struct Sha256::Impl {
    uint32_t state[8];
    uint64_t bit_length = 0;
    std::array<uint8_t, 64> buffer{};
    size_t buffer_len = 0;

    Impl() {
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
    }

    void process_block(const uint8_t* block) noexcept {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = h + s1 + ch + kRoundConstants[static_cast<size_t>(i)] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    // Feeds bytes into the block buffer without touching bit_length; used
    // both by update() (which does track length) and by finish()'s padding
    // (whose length contribution must NOT be counted).
    void absorb(const uint8_t* data, size_t len) noexcept {
        while (len > 0) {
            size_t take = std::min(len, size_t{64} - buffer_len);
            std::memcpy(buffer.data() + buffer_len, data, take);
            buffer_len += take;
            data += take;
            len -= take;
            if (buffer_len == 64) {
                process_block(buffer.data());
                buffer_len = 0;
            }
        }
    }

    void update(const uint8_t* data, size_t len) noexcept {
        bit_length += static_cast<uint64_t>(len) * 8;
        absorb(data, len);
    }

    std::array<uint8_t, 32> finish() noexcept {
        uint64_t total_bits = bit_length;

        uint8_t pad = 0x80;
        absorb(&pad, 1);
        uint8_t zero = 0;
        while (buffer_len != 56) {
            absorb(&zero, 1);
        }

        uint8_t len_bytes[8];
        for (int i = 0; i < 8; ++i) {
            len_bytes[i] = static_cast<uint8_t>(total_bits >> (56 - 8 * i));
        }
        absorb(len_bytes, 8);

        std::array<uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i) {
            digest[static_cast<size_t>(i) * 4 + 0] = static_cast<uint8_t>(state[i] >> 24);
            digest[static_cast<size_t>(i) * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
            digest[static_cast<size_t>(i) * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
            digest[static_cast<size_t>(i) * 4 + 3] = static_cast<uint8_t>(state[i]);
        }
        return digest;
    }
};

Sha256::Sha256() : impl_(std::make_unique<Impl>()) {}
Sha256::~Sha256() = default;

void Sha256::update(const void* data, size_t len) {
    impl_->update(static_cast<const uint8_t*>(data), len);
}

std::array<uint8_t, 32> Sha256::finish() {
    return impl_->finish();
}

namespace {

// Treats the wide path as UTF-8 bytes for std::filesystem::path, which is
// how this project's native (non-Windows) path value_type is used. This
// avoids std::filesystem's locale-dependent wide-to-native conversion.
std::filesystem::path native_path_from_wide(const std::wstring& path) {
    return std::filesystem::path(utf8_from_wide(path));
}

constexpr size_t kChunkSize = 256 * 1024;

}  // namespace

Result<std::array<uint8_t, 32>> sha256_file(const std::wstring& path) {
    std::ifstream stream(native_path_from_wide(path), std::ios::binary);
    if (!stream) {
        return make_error("ifstream::open", "failed to open source file for hashing");
    }

    Sha256 hasher;
    std::vector<char> buffer(kChunkSize);
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize got = stream.gcount();
        if (got > 0) {
            hasher.update(buffer.data(), static_cast<size_t>(got));
        }
    }
    if (stream.bad()) {
        return make_error("ifstream::read", "failed reading source file for hashing");
    }

    return hasher.finish();
}

}  // namespace wlm2pst

#endif  // !_WIN32
