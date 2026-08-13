// WLM2PST - minimal, fuzz-safe PE header parsing to confirm an executable's
// target architecture without loading it. Portable: no Windows headers.
#pragma once

#include <cstdint>
#include <vector>

namespace wlm2pst {

enum class PeArch { kUnknown, kX86, kX64, kArm64 };

// Parses a DOS header -> e_lfanew -> "PE\0\0" -> IMAGE_FILE_HEADER.Machine
// chain out of `prefix`, which should hold at least the first 4096 bytes of
// the file (fewer bytes simply yield kUnknown if the needed offsets fall
// outside what was captured). Every offset is bounds-checked against
// prefix.size() before use; malformed or truncated input never reads out of
// bounds and always yields kUnknown rather than throwing or asserting.
PeArch parse_pe_machine(const std::vector<uint8_t>& prefix);

}  // namespace wlm2pst
