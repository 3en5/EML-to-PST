// Portable helpers with no platform dependency at all. sha256_file() itself
// lives in sha256_win.cpp / sha256_portable.cpp because its I/O strategy is
// platform-specific (CreateFileW vs. std::ifstream); this file stays free
// of any OS header so it compiles unconditionally everywhere.
#include "common/hashing/sha256.h"

namespace wlm2pst {

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";
}  // namespace

std::string to_hex_lower(const uint8_t* data, size_t len) {
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = kHexDigits[data[i] >> 4];
        out[i * 2 + 1] = kHexDigits[data[i] & 0x0F];
    }
    return out;
}

}  // namespace wlm2pst
