// WLM2PST - unit tests for worker/folder_mapping/folder_collapse (spec section 9).
#include "worker/folder_mapping/folder_collapse.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace wlm2pst;

namespace {

std::wstring collapse_one(const std::wstring& dir, const CollapseOptions& options = {}) {
    return build_collapse_map({dir}, options).at(dir);
}

}  // namespace

TEST_CASE("built-in container segments are recognised", "[folder_collapse]") {
    CollapseOptions options;
    CHECK(is_container_segment(L"Storage Folders", options));
    CHECK(is_container_segment(L"storage folders (1)", options));
    CHECK(is_container_segment(L"Imported Folder", options));
    CHECK(is_container_segment(L"Recovered Items", options));
    CHECK(is_container_segment(L"פריטים משוחזרים", options));
    CHECK(is_container_segment(L"25-12-2023   f3", options));
    CHECK(is_container_segment(L"2023-12-25-074633", options));
}

TEST_CASE("real folders are never treated as containers", "[folder_collapse]") {
    CollapseOptions options;
    CHECK_FALSE(is_container_segment(L"Inbox", options));
    CHECK_FALSE(is_container_segment(L"Sent Items", options));
    CHECK_FALSE(is_container_segment(L"clients", options));
    CHECK_FALSE(is_container_segment(L"Storage Folders Backup", options));
    CHECK_FALSE(is_container_segment(L"2023 archive", options));  // a year, not a timestamp
    CHECK_FALSE(is_container_segment(L"", options));
}

TEST_CASE("container segments are dropped from the path", "[folder_collapse]") {
    CHECK(collapse_one(L"Storage Folders\\Inbox\\clients") == L"Inbox\\clients");
    CHECK(collapse_one(L"Storage Folders (1)\\פריטים משוחזרים\\"
                       L"25-12-2023   f3\\Storage Folders\\Inbox\\clients\\Field") ==
          L"Inbox\\clients\\Field");
}

TEST_CASE("a path made only of containers collapses to the root", "[folder_collapse]") {
    CHECK(collapse_one(L"Storage Folders\\Imported Folder") == L"");
}

TEST_CASE("extra patterns collapse account containers", "[folder_collapse]") {
    CollapseOptions options;
    options.extra_patterns = {L"example.net*"};
    CHECK(collapse_one(L"example.net ( b67\\Inbox", options) == L"Inbox");
    CHECK(collapse_one(L"EXAMPLE.NET (2)\\Inbox", options) == L"Inbox");
    CHECK(collapse_one(L"other.net\\Inbox", options) == L"other.net\\Inbox");
}

TEST_CASE("case variants merge into the first spelling", "[folder_collapse]") {
    const std::vector<std::wstring> dirs = {L"Sent items\\2020", L"Sent Items", L"Sent Items\\2020"};
    const auto map = build_collapse_map(dirs, {});
    CHECK(map.at(L"Sent Items") == L"Sent Items");
    CHECK(map.at(L"Sent Items\\2020") == L"Sent Items\\2020");
    CHECK(map.at(L"Sent items\\2020") == L"Sent Items\\2020");
}

TEST_CASE("case merging can be turned off", "[folder_collapse]") {
    CollapseOptions options;
    options.merge_case_variants = false;
    const std::vector<std::wstring> dirs = {L"Sent Items", L"Sent items"};
    const auto map = build_collapse_map(dirs, options);
    CHECK(map.at(L"Sent Items") == L"Sent Items");
    CHECK(map.at(L"Sent items") == L"Sent items");
}

TEST_CASE("collapsing can be turned off entirely", "[folder_collapse]") {
    CollapseOptions options;
    options.collapse_builtin_containers = false;
    options.merge_case_variants = false;
    CHECK(collapse_one(L"Storage Folders\\Inbox", options) == L"Storage Folders\\Inbox");
}

TEST_CASE("distinct sources may share one logical folder", "[folder_collapse]") {
    const std::vector<std::wstring> dirs = {
        L"Storage Folders\\Inbox",
        L"Storage Folders\\פריטים משוחזרים\\08-30-2022  90b\\Inbox",
    };
    const auto map = build_collapse_map(dirs, {});
    CHECK(map.at(dirs[0]) == L"Inbox");
    CHECK(map.at(dirs[1]) == L"Inbox");
}

TEST_CASE("option description is stable and order independent", "[folder_collapse]") {
    CollapseOptions a;
    a.extra_patterns = {L"B*", L"a"};
    CollapseOptions b;
    b.extra_patterns = {L"a", L"b*"};
    CHECK(describe_collapse_options(a) == describe_collapse_options(b));

    CollapseOptions off;
    off.collapse_builtin_containers = false;
    CHECK(describe_collapse_options(off) != describe_collapse_options(CollapseOptions{}));
}
