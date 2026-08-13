#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/hashing/sha256.h"
#include "common/unicode/utf.h"

using wlm2pst::Sha256;
using wlm2pst::sha256_file;
using wlm2pst::to_hex_lower;

namespace {

std::string hash_bytes(const void* data, size_t len) {
    Sha256 hasher;
    hasher.update(data, len);
    std::array<uint8_t, 32> digest = hasher.finish();
    return to_hex_lower(digest.data(), digest.size());
}

}  // namespace

TEST_CASE("SHA-256 NIST test vector: empty string", "[sha256]") {
    std::string hex = hash_bytes(nullptr, 0);
    CHECK(hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA-256 NIST test vector: \"abc\"", "[sha256]") {
    std::string input = "abc";
    std::string hex = hash_bytes(input.data(), input.size());
    CHECK(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA-256 NIST test vector: one million 'a'", "[sha256]") {
    std::string input(1'000'000, 'a');
    std::string hex = hash_bytes(input.data(), input.size());
    CHECK(hex == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 streaming update matches a single update", "[sha256]") {
    std::string input(1'000'000, 'a');

    Sha256 chunked;
    size_t chunk = 65536;
    for (size_t i = 0; i < input.size(); i += chunk) {
        size_t take = std::min(chunk, input.size() - i);
        chunked.update(input.data() + i, take);
    }
    std::array<uint8_t, 32> chunked_digest = chunked.finish();

    Sha256 whole;
    whole.update(input.data(), input.size());
    std::array<uint8_t, 32> whole_digest = whole.finish();

    CHECK(chunked_digest == whole_digest);
}

TEST_CASE("sha256_file streams a file from disk", "[sha256]") {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::filesystem::path file_path = dir / "wlm2pst_sha256_test_fixture.bin";

    {
        std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "abc";
    }

    std::wstring wide_path = wlm2pst::wide_from_utf8(file_path.string());
    auto result = sha256_file(wide_path);
    REQUIRE(result.ok());

    std::string hex = to_hex_lower(result.value().data(), result.value().size());
    CHECK(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    std::filesystem::remove(file_path);
}

TEST_CASE("sha256_file reports an error for a missing file", "[sha256]") {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::filesystem::path missing = dir / "wlm2pst_sha256_test_does_not_exist.bin";
    std::filesystem::remove(missing);

    std::wstring wide_path = wlm2pst::wide_from_utf8(missing.string());
    auto result = sha256_file(wide_path);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("to_hex_lower formats bytes as lowercase hex", "[sha256]") {
    std::array<uint8_t, 4> bytes{0x00, 0xAB, 0xFF, 0x10};
    CHECK(to_hex_lower(bytes.data(), bytes.size()) == "00abff10");
}
