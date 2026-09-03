// WLM2PST - unit tests for worker/addressing/one_off_entryid.
//
// These assert the byte layout directly. The first version of this code wrote
// the 16-bit version and flags fields as one 32-bit value, which put the flags
// in the version slot and left the flags zero - claiming 8-bit strings for the
// UTF-16 that followed. Outlook still displayed the address (it reads that
// from the row properties), so the defect was invisible until a user tried
// "Add to Contacts" and got MAPI_E_NO_SUPPORT.
#include "worker/addressing/one_off_entryid.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using wlm2pst::build_one_off_entryid;

namespace {

std::uint16_t u16_at(const std::vector<std::uint8_t>& v, size_t offset) {
    return static_cast<std::uint16_t>(v[offset] | (v[offset + 1] << 8));
}

std::wstring utf16_at(const std::vector<std::uint8_t>& v, size_t offset, size_t* next = nullptr) {
    std::wstring out;
    for (;; offset += 2) {
        std::uint16_t c = u16_at(v, offset);
        if (c == 0) break;
        out.push_back(static_cast<wchar_t>(c));
    }
    if (next != nullptr) *next = offset + 2;
    return out;
}

}  // namespace

TEST_CASE("one-off entry id has the documented layout", "[addressing]") {
    const auto eid = build_one_off_entryid(L"Rina", L"rina@example.com");

    // ULONG flags = 0
    for (size_t i = 0; i < 4; ++i) CHECK(eid[i] == 0);

    // MAPIUID {A41F2B81-A3BE-1910-9D6E-00DD010F5402}
    const std::uint8_t expected_uid[16] = {0x81, 0x2B, 0x1F, 0xA4, 0xBE, 0xA3, 0x10, 0x19,
                                           0x9D, 0x6E, 0x00, 0xDD, 0x01, 0x0F, 0x54, 0x02};
    for (size_t i = 0; i < 16; ++i) CHECK(eid[4 + i] == expected_uid[i]);

    // USHORT version = 0, then USHORT flags - two 16-bit fields, not one 32-bit
    CHECK(u16_at(eid, 20) == 0x0000);
    CHECK(u16_at(eid, 22) == 0x8001);  // MAPI_ONE_OFF_UNICODE | MAPI_SEND_NO_RICH_INFO

    // The Unicode bit must be set, or MAPI reads the strings that follow as 8-bit
    CHECK((u16_at(eid, 22) & 0x8000) != 0);

    size_t pos = 24;
    CHECK(utf16_at(eid, pos, &pos) == L"Rina");
    CHECK(utf16_at(eid, pos, &pos) == L"SMTP");
    CHECK(utf16_at(eid, pos, &pos) == L"rina@example.com");
    CHECK(pos == eid.size());
}

TEST_CASE("one-off entry id carries non-ASCII display names", "[addressing]") {
    const auto eid = build_one_off_entryid(L"רינה", L"r@example.com");
    size_t pos = 24;
    CHECK(utf16_at(eid, pos, &pos) == L"רינה");
    CHECK(utf16_at(eid, pos, &pos) == L"SMTP");
    CHECK(utf16_at(eid, pos, &pos) == L"r@example.com");
}

TEST_CASE("one-off entry id tolerates an empty display name", "[addressing]") {
    const auto eid = build_one_off_entryid(L"", L"r@example.com");
    size_t pos = 24;
    CHECK(utf16_at(eid, pos, &pos).empty());
    CHECK(utf16_at(eid, pos, &pos) == L"SMTP");
    CHECK(utf16_at(eid, pos, &pos) == L"r@example.com");
    CHECK(pos == eid.size());
}
