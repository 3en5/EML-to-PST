#include "worker/dates/date_parser.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace wlm2pst {

namespace {

char to_upper_ch(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

bool is_digit_ch(char c) {
    return c >= '0' && c <= '9';
}

std::string to_upper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(to_upper_ch(c));
    return out;
}

// Removes nested parenthesized comments anywhere in `text`, honoring
// backslash quoted-pairs inside comments (RFC 5322 3.2.2). Everything
// outside comments is copied through unchanged.
std::string strip_comments(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (depth == 0) {
            if (c == '(') {
                ++depth;
            } else {
                out.push_back(c);
            }
        } else {
            if (c == '\\' && i + 1 < text.size()) {
                ++i;  // skip the escaped character too
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            }
            // Any other character while inside a comment is dropped.
        }
    }
    return out;
}

bool is_ws_ch(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::vector<std::string_view> tokenize(std::string_view s) {
    std::vector<std::string_view> tokens;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_ws_ch(s[i])) ++i;
        size_t start = i;
        while (i < s.size() && !is_ws_ch(s[i])) ++i;
        if (i > start) tokens.push_back(s.substr(start, i - start));
    }
    return tokens;
}

// Parses `tok` as an unsigned decimal integer with length in
// [min_len, max_len], all-digit. Fails (returns false) otherwise.
bool parse_digits(std::string_view tok, size_t min_len, size_t max_len, int& out) {
    if (tok.size() < min_len || tok.size() > max_len) return false;
    int value = 0;
    for (char c : tok) {
        if (!is_digit_ch(c)) return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool is_day_name(std::string_view core) {
    static constexpr std::array<std::string_view, 7> kDays = {"MON", "TUE", "WED", "THU",
                                                                "FRI", "SAT", "SUN"};
    if (core.size() != 3) return false;
    std::string upper = to_upper(core);
    for (auto d : kDays) {
        if (upper == d) return true;
    }
    return false;
}

// Returns 1-12, or 0 on failure. Matches on the first three letters
// case-insensitively so both "Jul" and "July" work.
int parse_month(std::string_view tok) {
    static constexpr std::array<std::string_view, 12> kMonths = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (tok.size() < 3) return 0;
    std::string upper = to_upper(tok.substr(0, 3));
    for (size_t i = 0; i < kMonths.size(); ++i) {
        if (upper == kMonths[i]) return static_cast<int>(i) + 1;
    }
    return 0;
}

bool parse_year(std::string_view tok, int& year) {
    if (tok.size() < 2 || tok.size() > 4) return false;
    int v = 0;
    if (!parse_digits(tok, tok.size(), tok.size(), v)) return false;
    if (tok.size() == 2) {
        year = (v <= 49) ? 2000 + v : 1900 + v;
    } else if (tok.size() == 3) {
        year = 1900 + v;
    } else {
        year = v;
    }
    return true;
}

bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) {
    static constexpr std::array<int, 12> kDim = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(year)) return 29;
    return kDim[static_cast<size_t>(month - 1)];
}

// Howard Hinnant's days-from-civil algorithm: days since 1970-01-01 for a
// proleptic-Gregorian civil date. y may be negative; m in [1,12]; d is the
// day of month (caller has already range-checked it against days_in_month).
constexpr int64_t days_from_civil(int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);                  // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;         // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                  // [0, 146096]
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool parse_time(std::string_view tok, int& hh, int& mm, int& ss) {
    size_t c1 = tok.find(':');
    if (c1 == std::string_view::npos) return false;
    std::string_view hpart = tok.substr(0, c1);
    std::string_view rest = tok.substr(c1 + 1);

    std::string_view mpart = rest;
    std::string_view spart;
    bool has_sec = false;
    size_t c2 = rest.find(':');
    if (c2 != std::string_view::npos) {
        mpart = rest.substr(0, c2);
        spart = rest.substr(c2 + 1);
        has_sec = true;
    }

    if (!parse_digits(hpart, 1, 2, hh) || hh > 23) return false;
    if (!parse_digits(mpart, 1, 2, mm) || mm > 59) return false;
    if (has_sec) {
        if (!parse_digits(spart, 1, 2, ss) || ss > 60) return false;
    } else {
        ss = 0;
    }
    return true;
}

// Parses a zone token into a signed offset in minutes from UTC. Handles
// numeric "+HHMM"/"-HHMM" (rejecting minutes >= 60 or |hours| > 14),
// obsolete named zones, "UT"/"GMT"/"Z" (all +0000), and single-letter
// military zones (treated as +0000 per RFC 5322's recommendation, since
// their historical meanings are unreliable).
bool parse_zone(std::string_view tok, int& offset_minutes) {
    if (tok.size() == 5 && (tok[0] == '+' || tok[0] == '-')) {
        bool all_digit = true;
        for (size_t i = 1; i < 5; ++i) {
            if (!is_digit_ch(tok[i])) all_digit = false;
        }
        if (!all_digit) return false;
        int hh = (tok[1] - '0') * 10 + (tok[2] - '0');
        int mm = (tok[3] - '0') * 10 + (tok[4] - '0');
        if (mm >= 60 || hh > 14) return false;
        int sign = (tok[0] == '-') ? -1 : 1;
        offset_minutes = sign * (hh * 60 + mm);
        return true;
    }

    static constexpr std::array<std::pair<std::string_view, int>, 11> kNamed = {{
        {"UT", 0},
        {"GMT", 0},
        {"Z", 0},
        {"EST", -5 * 60},
        {"EDT", -4 * 60},
        {"CST", -6 * 60},
        {"CDT", -5 * 60},
        {"MST", -7 * 60},
        {"MDT", -6 * 60},
        {"PST", -8 * 60},
        {"PDT", -7 * 60},
    }};
    std::string upper = to_upper(tok);
    for (const auto& [name, minutes] : kNamed) {
        if (upper == name) {
            offset_minutes = minutes;
            return true;
        }
    }

    if (tok.size() == 1 && std::isalpha(static_cast<unsigned char>(tok[0]))) {
        offset_minutes = 0;
        return true;
    }

    return false;
}

}  // namespace

std::optional<FileTimeUtc> parse_rfc5322_date(std::string_view text) {
    std::string stripped = strip_comments(text);
    std::vector<std::string_view> tokens = tokenize(stripped);
    if (tokens.empty()) return std::nullopt;

    size_t idx = 0;
    {
        std::string_view t = tokens[0];
        if (!t.empty() && t.back() == ',') t.remove_suffix(1);
        if (is_day_name(t)) idx = 1;
    }

    // Require day, month, year, time; zone is optional.
    if (tokens.size() < idx + 4) return std::nullopt;
    if (tokens.size() > idx + 5) return std::nullopt;

    std::string_view day_tok = tokens[idx + 0];
    std::string_view month_tok = tokens[idx + 1];
    std::string_view year_tok = tokens[idx + 2];
    std::string_view time_tok = tokens[idx + 3];
    bool has_zone = tokens.size() == idx + 5;
    std::string_view zone_tok = has_zone ? tokens[idx + 4] : std::string_view{};

    int day = 0;
    if (!parse_digits(day_tok, 1, 2, day) || day < 1 || day > 31) return std::nullopt;

    int month = parse_month(month_tok);
    if (month == 0) return std::nullopt;

    int year = 0;
    if (!parse_year(year_tok, year)) return std::nullopt;

    if (day > days_in_month(year, month)) return std::nullopt;

    int hh = 0, mm = 0, ss = 0;
    if (!parse_time(time_tok, hh, mm, ss)) return std::nullopt;

    int offset_minutes = 0;
    if (has_zone) {
        if (!parse_zone(zone_tok, offset_minutes)) return std::nullopt;
    }

    const int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const int64_t clamped_seconds = ss > 59 ? 59 : ss;  // leap second clamp
    const int64_t local_seconds = days * 86400 + hh * 3600 + mm * 60 + clamped_seconds;
    const int64_t unix_seconds = local_seconds - static_cast<int64_t>(offset_minutes) * 60;

    return filetime_from_unix_seconds(unix_seconds);
}

std::optional<FileTimeUtc> parse_rfc5322_date_bounded(std::string_view text, FileTimeUtc now_utc) {
    auto parsed = parse_rfc5322_date(text);
    if (!parsed) return std::nullopt;

    if (*parsed < filetime_from_unix_seconds(0)) return std::nullopt;

    constexpr int64_t kSecondsPerYearApprox = 365LL * 86400;
    const FileTimeUtc upper_bound = now_utc + kSecondsPerYearApprox * kFiletimeTicksPerSecond;
    if (*parsed > upper_bound) return std::nullopt;

    return parsed;
}

}  // namespace wlm2pst
