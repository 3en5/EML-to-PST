#include <cstdint>
#include <string>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "common/unicode/utf.h"

using wlm2pst::compare_ordinal;
using wlm2pst::compare_ordinal_ignore_case;
using wlm2pst::utf8_from_wide;
using wlm2pst::wide_from_utf8;

namespace {

// U+FFFD as an unsigned code unit value, robust to wchar_t being signed.
uint32_t code_unit(wchar_t c) noexcept {
    return static_cast<uint32_t>(static_cast<std::make_unsigned_t<wchar_t>>(c));
}

std::wstring hebrew_shalom_wide() {
    // "שלום": U+05E9 U+05DC U+05D5 U+05DD, built from code points so the
    // test does not depend on this source file's own encoding.
    std::wstring w;
    w.push_back(static_cast<wchar_t>(0x05E9));
    w.push_back(static_cast<wchar_t>(0x05DC));
    w.push_back(static_cast<wchar_t>(0x05D5));
    w.push_back(static_cast<wchar_t>(0x05DD));
    return w;
}

std::string hebrew_shalom_utf8() {
    // Same string as UTF-8 bytes: each Hebrew letter is a 2-byte sequence.
    return std::string{
        static_cast<char>(0xD7), static_cast<char>(0xA9),  // U+05E9
        static_cast<char>(0xD7), static_cast<char>(0x9C),  // U+05DC
        static_cast<char>(0xD7), static_cast<char>(0x95),  // U+05D5
        static_cast<char>(0xD7), static_cast<char>(0x9D),  // U+05DD
    };
}

}  // namespace

TEST_CASE("UTF-8 <-> wide round-trip: ASCII", "[unicode]") {
    std::wstring wide = L"Hello, WLM2PST!";
    std::string utf8 = utf8_from_wide(wide);
    REQUIRE(utf8 == "Hello, WLM2PST!");
    REQUIRE(wide_from_utf8(utf8) == wide);
}

TEST_CASE("UTF-8 <-> wide round-trip: Hebrew (RTL, non-ASCII)", "[unicode]") {
    std::wstring wide = hebrew_shalom_wide();
    std::string expected_utf8 = hebrew_shalom_utf8();

    REQUIRE(utf8_from_wide(wide) == expected_utf8);
    REQUIRE(wide_from_utf8(expected_utf8) == wide);
}

TEST_CASE("UTF-8 <-> wide round-trip: emoji outside the BMP", "[unicode]") {
    // U+1F600 GRINNING FACE, UTF-8 bytes: F0 9F 98 80.
    std::string utf8_emoji{static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98),
                            static_cast<char>(0x80)};

    std::wstring wide = wide_from_utf8(utf8_emoji);
    if constexpr (sizeof(wchar_t) == 2) {
        REQUIRE(wide.size() == 2);  // UTF-16 surrogate pair.
        CHECK(code_unit(wide[0]) == 0xD83Du);
        CHECK(code_unit(wide[1]) == 0xDE00u);
    } else {
        REQUIRE(wide.size() == 1);
        CHECK(code_unit(wide[0]) == 0x1F600u);
    }
    REQUIRE(utf8_from_wide(wide) == utf8_emoji);
}

TEST_CASE("wide_from_utf8: invalid sequences become U+FFFD, never throw", "[unicode]") {
    SECTION("lone continuation byte") {
        std::string bad{static_cast<char>(0x80)};
        std::wstring wide = wide_from_utf8(bad);
        REQUIRE(wide.size() == 1);
        CHECK(code_unit(wide[0]) == 0xFFFDu);
    }

    SECTION("invalid leading byte") {
        std::string bad{static_cast<char>(0xFF)};
        std::wstring wide = wide_from_utf8(bad);
        REQUIRE(wide.size() == 1);
        CHECK(code_unit(wide[0]) == 0xFFFDu);
    }

    SECTION("overlong encoding of NUL") {
        std::string bad{static_cast<char>(0xC0), static_cast<char>(0x80)};
        std::wstring wide = wide_from_utf8(bad);
        REQUIRE(wide.size() == 1);
        CHECK(code_unit(wide[0]) == 0xFFFDu);
    }

    SECTION("truncated multi-byte sequence at end of input") {
        std::string bad{static_cast<char>(0xE0), static_cast<char>(0xA0)};  // missing 3rd byte
        std::wstring wide = wide_from_utf8(bad);
        REQUIRE(wide.size() == 1);
        CHECK(code_unit(wide[0]) == 0xFFFDu);
    }

    SECTION("UTF-8-encoded surrogate value is rejected") {
        // U+D800 encoded directly as a 3-byte sequence (ED A0 80) is invalid
        // UTF-8 (surrogates must never appear encoded in UTF-8).
        std::string bad{static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80)};
        std::wstring wide = wide_from_utf8(bad);
        REQUIRE(wide.size() == 1);
        CHECK(code_unit(wide[0]) == 0xFFFDu);
    }

    SECTION("valid text survives around a bad byte") {
        std::string mixed = "ok-";
        mixed.push_back(static_cast<char>(0xFF));
        mixed += "-end";
        std::wstring wide = wide_from_utf8(mixed);
        std::string back = utf8_from_wide(wide);
        REQUIRE(back.substr(0, 3) == "ok-");
        REQUIRE(back.substr(back.size() - 4) == "-end");
    }
}

TEST_CASE("compare_ordinal orders by raw code unit", "[unicode]") {
    CHECK(compare_ordinal(L"abc", L"abc") == 0);
    CHECK(compare_ordinal(L"abc", L"abd") < 0);
    CHECK(compare_ordinal(L"abd", L"abc") > 0);
    CHECK(compare_ordinal(L"ab", L"abc") < 0);
    CHECK(compare_ordinal(L"abc", L"ab") > 0);
    CHECK(compare_ordinal(L"ABC", L"abc") < 0);  // 'A' (0x41) < 'a' (0x61), no folding
    CHECK(compare_ordinal(L"", L"") == 0);
}

TEST_CASE("compare_ordinal_ignore_case folds ASCII only", "[unicode]") {
    CHECK(compare_ordinal_ignore_case(L"ABC", L"abc") == 0);
    CHECK(compare_ordinal_ignore_case(L"Inbox", L"INBOX") == 0);
    CHECK(compare_ordinal_ignore_case(L"Inbox", L"inbox") == 0);
    CHECK(compare_ordinal_ignore_case(L"abc", L"abd") < 0);
    CHECK(compare_ordinal_ignore_case(L"abd", L"abc") > 0);
    CHECK(compare_ordinal_ignore_case(L"", L"") == 0);
    CHECK(compare_ordinal_ignore_case(L"a", L"") > 0);

    // Non-ASCII code units are documented as NOT folded: an accented Latin-1
    // pair that does have real case still compares unequal here, showing the
    // pass-through contract is intentional rather than accidental.
    std::wstring lower_e_acute(1, static_cast<wchar_t>(0x00E9));  // é
    std::wstring upper_e_acute(1, static_cast<wchar_t>(0x00C9));  // É
    CHECK(compare_ordinal_ignore_case(lower_e_acute, upper_e_acute) != 0);
}
