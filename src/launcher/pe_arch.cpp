#include "launcher/pe_arch.h"

#include <cstring>

namespace wlm2pst {

namespace {

constexpr size_t kDosHeaderSize = 0x40;   // Enough to cover e_lfanew at 0x3C.
constexpr size_t kElfanewOffset = 0x3C;
constexpr uint16_t kDosMagic = 0x5A4D;    // "MZ"
constexpr uint8_t kPeSignature[4] = {'P', 'E', 0x00, 0x00};

constexpr uint16_t kMachineX86 = 0x014c;
constexpr uint16_t kMachineX64 = 0x8664;
constexpr uint16_t kMachineArm64 = 0xAA64;

uint16_t read_u16(const std::vector<uint8_t>& buf, size_t offset) {
    // Caller guarantees offset + 2 <= buf.size().
    return static_cast<uint16_t>(buf[offset]) | (static_cast<uint16_t>(buf[offset + 1]) << 8);
}

uint32_t read_u32(const std::vector<uint8_t>& buf, size_t offset) {
    // Caller guarantees offset + 4 <= buf.size().
    return static_cast<uint32_t>(buf[offset]) | (static_cast<uint32_t>(buf[offset + 1]) << 8) |
           (static_cast<uint32_t>(buf[offset + 2]) << 16) |
           (static_cast<uint32_t>(buf[offset + 3]) << 24);
}

}  // namespace

PeArch parse_pe_machine(const std::vector<uint8_t>& prefix) {
    if (prefix.size() < kDosHeaderSize) {
        return PeArch::kUnknown;
    }
    if (read_u16(prefix, 0) != kDosMagic) {
        return PeArch::kUnknown;
    }

    uint32_t e_lfanew = read_u32(prefix, kElfanewOffset);

    // Sanity bound: e_lfanew is a signed 32-bit field in the real header;
    // reject values that would be negative when reinterpreted, and reject
    // anything that does not leave room for a 4-byte signature + 2-byte
    // Machine field within the captured prefix.
    if (e_lfanew > 0x7FFFFFFFu) {
        return PeArch::kUnknown;
    }
    size_t sig_offset = static_cast<size_t>(e_lfanew);

    // Need sig_offset + 4 (signature) + 2 (Machine) to fit in the buffer.
    // Guard against overflow before adding.
    if (sig_offset > prefix.size() || prefix.size() - sig_offset < 6) {
        return PeArch::kUnknown;
    }

    if (std::memcmp(prefix.data() + sig_offset, kPeSignature, sizeof(kPeSignature)) != 0) {
        return PeArch::kUnknown;
    }

    size_t machine_offset = sig_offset + sizeof(kPeSignature);
    uint16_t machine = read_u16(prefix, machine_offset);

    switch (machine) {
        case kMachineX86: return PeArch::kX86;
        case kMachineX64: return PeArch::kX64;
        case kMachineArm64: return PeArch::kArm64;
        default: return PeArch::kUnknown;
    }
}

}  // namespace wlm2pst
