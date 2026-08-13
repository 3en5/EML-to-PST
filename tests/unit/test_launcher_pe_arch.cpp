#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "launcher/pe_arch.h"

using wlm2pst::PeArch;
using wlm2pst::parse_pe_machine;

namespace {

constexpr size_t kElfanewOffset = 0x3C;
constexpr size_t kPeHeaderOffset = 0x40;  // Where we place "PE\0\0" in our fixtures.

void put_u32_le(std::vector<uint8_t>& buf, size_t offset, uint32_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void put_u16_le(std::vector<uint8_t>& buf, size_t offset, uint16_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

// Builds a minimal, well-formed PE prefix: MZ header -> e_lfanew=0x40 ->
// "PE\0\0" -> Machine field, for the given machine value.
std::vector<uint8_t> make_valid_prefix(uint16_t machine) {
    std::vector<uint8_t> buf(0x80, 0);
    buf[0] = 'M';
    buf[1] = 'Z';
    put_u32_le(buf, kElfanewOffset, static_cast<uint32_t>(kPeHeaderOffset));
    buf[kPeHeaderOffset + 0] = 'P';
    buf[kPeHeaderOffset + 1] = 'E';
    buf[kPeHeaderOffset + 2] = 0x00;
    buf[kPeHeaderOffset + 3] = 0x00;
    put_u16_le(buf, kPeHeaderOffset + 4, machine);
    return buf;
}

}  // namespace

TEST_CASE("parse_pe_machine recognizes x86", "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x014c);
    REQUIRE(parse_pe_machine(prefix) == PeArch::kX86);
}

TEST_CASE("parse_pe_machine recognizes x64", "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x8664);
    REQUIRE(parse_pe_machine(prefix) == PeArch::kX64);
}

TEST_CASE("parse_pe_machine recognizes arm64", "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0xAA64);
    REQUIRE(parse_pe_machine(prefix) == PeArch::kArm64);
}

TEST_CASE("parse_pe_machine rejects unknown machine values", "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x1234);
    REQUIRE(parse_pe_machine(prefix) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects empty input", "[launcher][pe_arch]") {
    REQUIRE(parse_pe_machine({}) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects truncated DOS header", "[launcher][pe_arch]") {
    std::vector<uint8_t> prefix = {'M', 'Z', 1, 2, 3};  // Far shorter than 0x40 bytes.
    REQUIRE(parse_pe_machine(prefix) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects missing MZ magic", "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x014c);
    prefix[0] = 'X';
    REQUIRE(parse_pe_machine(prefix) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects garbage PE signature", "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x014c);
    prefix[kPeHeaderOffset] = 'X';  // Corrupt "PE\0\0" -> "XE\0\0".
    REQUIRE(parse_pe_machine(prefix) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects e_lfanew of zero (points back into DOS header)",
          "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x014c);
    put_u32_le(prefix, kElfanewOffset, 0);
    REQUIRE(parse_pe_machine(prefix) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects e_lfanew pointing past the captured buffer",
          "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x014c);
    put_u32_le(prefix, kElfanewOffset, 0xFFFF0000u);
    REQUIRE(parse_pe_machine(prefix) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects e_lfanew that would be negative as int32",
          "[launcher][pe_arch]") {
    auto prefix = make_valid_prefix(0x014c);
    put_u32_le(prefix, kElfanewOffset, 0xFFFFFFFFu);
    REQUIRE(parse_pe_machine(prefix) == PeArch::kUnknown);
}

TEST_CASE("parse_pe_machine rejects e_lfanew leaving no room for the Machine field",
          "[launcher][pe_arch]") {
    // Signature would fit but the 2-byte Machine field would run past the end.
    std::vector<uint8_t> buf(0x46, 0);
    buf[0] = 'M';
    buf[1] = 'Z';
    put_u32_le(buf, kElfanewOffset, static_cast<uint32_t>(kPeHeaderOffset));
    buf[kPeHeaderOffset + 0] = 'P';
    buf[kPeHeaderOffset + 1] = 'E';
    buf[kPeHeaderOffset + 2] = 0x00;
    buf[kPeHeaderOffset + 3] = 0x00;
    // Buffer ends right after the signature; only 1 byte left for Machine.
    buf.resize(kPeHeaderOffset + 5);
    REQUIRE(parse_pe_machine(buf) == PeArch::kUnknown);
}
