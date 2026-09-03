#include "worker/addressing/one_off_entryid.h"

#include <cstring>

namespace wlm2pst {
namespace {

constexpr std::uint8_t kOneOffProviderUid[16] = {0x81, 0x2B, 0x1F, 0xA4, 0xBE, 0xA3, 0x10, 0x19,
                                                 0x9D, 0x6E, 0x00, 0xDD, 0x01, 0x0F, 0x54, 0x02};
constexpr std::uint16_t kMapiOneOffUnicode = 0x8000;
constexpr std::uint16_t kMapiSendNoRichInfo = 0x0001;

void append_u16(std::vector<std::uint8_t>& v, std::uint16_t value) {
    v.push_back(static_cast<std::uint8_t>(value & 0xFF));
    v.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void append_u32(std::vector<std::uint8_t>& v, std::uint32_t value) {
    append_u16(v, static_cast<std::uint16_t>(value & 0xFFFF));
    append_u16(v, static_cast<std::uint16_t>((value >> 16) & 0xFFFF));
}

// UTF-16LE, NUL terminated. Written byte by byte so the layout does not depend
// on the width or endianness of wchar_t on the building platform.
void append_utf16(std::vector<std::uint8_t>& v, const std::wstring& s) {
    for (wchar_t c : s) append_u16(v, static_cast<std::uint16_t>(c));
    append_u16(v, 0);
}

}  // namespace

std::vector<std::uint8_t> build_one_off_entryid(const std::wstring& display_name,
                                                const std::wstring& address) {
    static const std::wstring kAddressType = L"SMTP";

    std::vector<std::uint8_t> eid;
    eid.reserve(28 + (display_name.size() + kAddressType.size() + address.size() + 3) * 2);
    append_u32(eid, 0);  // flags
    eid.insert(eid.end(), std::begin(kOneOffProviderUid), std::end(kOneOffProviderUid));
    append_u16(eid, 0);  // version
    append_u16(eid, kMapiOneOffUnicode | kMapiSendNoRichInfo);
    append_utf16(eid, display_name);
    append_utf16(eid, kAddressType);
    append_utf16(eid, address);
    return eid;
}

}  // namespace wlm2pst
