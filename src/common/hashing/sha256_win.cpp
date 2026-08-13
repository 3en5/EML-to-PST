// Windows implementation of Sha256 using CNG (bcrypt.dll), plus a
// CreateFileW-based sha256_file() that opens the source file read-only
// while denying writes (spec section 7/24).
#include "common/hashing/sha256.h"

#include <algorithm>
#include <vector>

#include "common/win_compat.h"

#include <bcrypt.h>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif

namespace wlm2pst {

struct Sha256::Impl {
    BCRYPT_ALG_HANDLE alg_handle = nullptr;
    BCRYPT_HASH_HANDLE hash_handle = nullptr;
    std::vector<uint8_t> hash_object_buffer;

    Impl() {
        if (!BCRYPT_SUCCESS(
                BCryptOpenAlgorithmProvider(&alg_handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            alg_handle = nullptr;
            return;
        }

        DWORD object_length = 0;
        ULONG copied = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(alg_handle, BCRYPT_OBJECT_LENGTH,
                                               reinterpret_cast<PUCHAR>(&object_length),
                                               sizeof(object_length), &copied, 0))) {
            BCryptCloseAlgorithmProvider(alg_handle, 0);
            alg_handle = nullptr;
            return;
        }

        hash_object_buffer.resize(object_length);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(alg_handle, &hash_handle, hash_object_buffer.data(),
                                              object_length, nullptr, 0, 0))) {
            hash_handle = nullptr;
            BCryptCloseAlgorithmProvider(alg_handle, 0);
            alg_handle = nullptr;
        }
    }

    ~Impl() {
        if (hash_handle != nullptr) {
            BCryptDestroyHash(hash_handle);
        }
        if (alg_handle != nullptr) {
            BCryptCloseAlgorithmProvider(alg_handle, 0);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void update(const uint8_t* data, size_t len) noexcept {
        if (hash_handle == nullptr || len == 0) {
            return;
        }
        const uint8_t* cursor = data;
        size_t remaining = len;
        while (remaining > 0) {
            ULONG chunk = static_cast<ULONG>(std::min<size_t>(remaining, 0xFFFFFFFFu));
            BCryptHashData(hash_handle, const_cast<PUCHAR>(cursor), chunk, 0);
            cursor += chunk;
            remaining -= chunk;
        }
    }

    std::array<uint8_t, 32> finish() noexcept {
        std::array<uint8_t, 32> digest{};
        if (hash_handle != nullptr) {
            BCryptFinishHash(hash_handle, digest.data(), static_cast<ULONG>(digest.size()), 0);
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
constexpr size_t kChunkSize = 256 * 1024;
}  // namespace

Result<std::array<uint8_t, 32>> sha256_file(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return make_win32_error(GetLastError(), "CreateFileW",
                                 "failed to open source file for hashing");
    }

    Sha256 hasher;
    std::vector<uint8_t> buffer(kChunkSize);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            DWORD err = GetLastError();
            CloseHandle(file);
            return make_win32_error(err, "ReadFile", "failed reading source file for hashing");
        }
        if (read == 0) {
            break;
        }
        hasher.update(buffer.data(), read);
    }
    CloseHandle(file);
    return hasher.finish();
}

}  // namespace wlm2pst
