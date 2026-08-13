// WLM2PST - unit tests for worker/folder_mapping/folder_mapper (spec sections
// 9, 28).
#include "worker/folder_mapping/folder_mapper.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace wlm2pst;

namespace {

const MappedFolder* find_folder(const FolderPlan& plan, const std::wstring& source_relative) {
    for (const auto& f : plan.folders) {
        if (f.source_relative == source_relative) return &f;
    }
    return nullptr;
}

std::wstring joined(const std::vector<std::wstring>& segments) {
    std::wstring out;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i != 0) out += L"\\";
        out += segments[i];
    }
    return out;
}

}  // namespace

TEST_CASE("root eml maps to the synthetic Root Messages folder", "[folder_mapping]") {
    auto result = build_folder_plan({L""});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 1);
    const auto& f = result.value().folders[0];
    CHECK(f.source_relative == L"");
    CHECK(f.target_segments == std::vector<std::wstring>{kRootMessagesFolder});
    CHECK(result.value().renames.empty());
}

TEST_CASE("nested dirs derive their ancestor chain automatically", "[folder_mapping]") {
    auto result = build_folder_plan({L"A\\B\\C"});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 3);

    const auto* a = find_folder(result.value(), L"A");
    const auto* ab = find_folder(result.value(), L"A\\B");
    const auto* abc = find_folder(result.value(), L"A\\B\\C");
    REQUIRE(a != nullptr);
    REQUIRE(ab != nullptr);
    REQUIRE(abc != nullptr);
    CHECK(joined(a->target_segments) == L"A");
    CHECK(joined(ab->target_segments) == L"A\\B");
    CHECK(joined(abc->target_segments) == L"A\\B\\C");

    // Deterministic order: parents before children.
    CHECK(result.value().folders[0].source_relative == L"A");
    CHECK(result.value().folders[1].source_relative == L"A\\B");
    CHECK(result.value().folders[2].source_relative == L"A\\B\\C");
}

TEST_CASE("shared ancestors are not duplicated across multiple leaf dirs", "[folder_mapping]") {
    auto result = build_folder_plan({L"A\\B\\C", L"A\\B\\D", L""});
    REQUIRE(result.ok());
    // "", A, A\B, A\B\C, A\B\D.
    CHECK(result.value().folders.size() == 5);
}

TEST_CASE("trailing spaces and periods are trimmed", "[folder_mapping]") {
    auto result = build_folder_plan({L"Customers.. "});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 1);
    CHECK(joined(result.value().folders[0].target_segments) == L"Customers");
    REQUIRE(result.value().renames.size() == 1);
    CHECK(result.value().renames[0].reason == "trailing-trim");
    CHECK(result.value().renames[0].source_relative_folder == L"Customers.. ");
    CHECK(result.value().renames[0].target_relative_folder == L"Customers");
}

TEST_CASE("control characters are replaced with underscore", "[folder_mapping]") {
    std::wstring name = L"Bad";
    name.push_back(L'\x01');
    name += L"Name";
    auto result = build_folder_plan({name});
    REQUIRE(result.ok());
    CHECK(joined(result.value().folders[0].target_segments) == L"Bad_Name");
    REQUIRE(result.value().renames.size() == 1);
    CHECK(result.value().renames[0].reason == "control-chars");
}

TEST_CASE("a name that becomes empty after trimming falls back to underscore", "[folder_mapping]") {
    auto result = build_folder_plan({L"..."});
    REQUIRE(result.ok());
    CHECK(joined(result.value().folders[0].target_segments) == L"_");
    CHECK_FALSE(result.value().renames.empty());
}

TEST_CASE("over-long names are truncated with a stable hash suffix", "[folder_mapping]") {
    std::wstring longname(150, L'x');
    auto result1 = build_folder_plan({longname});
    auto result2 = build_folder_plan({longname});  // same input -> same hash
    REQUIRE(result1.ok());
    REQUIRE(result2.ok());

    std::wstring target1 = joined(result1.value().folders[0].target_segments);
    std::wstring target2 = joined(result2.value().folders[0].target_segments);
    CHECK(target1 == target2);
    CHECK(target1.size() == 100 + 1 + 8);  // truncated(100) + '~' + 8 hex chars
    CHECK(target1.substr(0, 100) == std::wstring(100, L'x'));
    CHECK(target1[100] == L'~');

    bool found_too_long = false;
    for (const auto& r : result1.value().renames) {
        if (r.reason == "too-long") found_too_long = true;
    }
    CHECK(found_too_long);
}

TEST_CASE("different over-long names produce different hash suffixes", "[folder_mapping]") {
    std::wstring name1(150, L'x');
    std::wstring name2(150, L'y');
    auto result = build_folder_plan({name1, name2});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 2);
    std::wstring t1 = joined(result.value().folders[0].target_segments);
    std::wstring t2 = joined(result.value().folders[1].target_segments);
    CHECK(t1 != t2);
}

TEST_CASE("case-only sibling collisions resolve to distinct targets", "[folder_mapping]") {
    auto result = build_folder_plan({L"Customers", L"customers"});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 2);

    std::vector<std::wstring> targets;
    for (const auto& f : result.value().folders) targets.push_back(joined(f.target_segments));
    CHECK(targets[0] != targets[1]);

    int bare = 0, suffixed = 0;
    for (const auto& t : targets) {
        if (t == L"Customers" || t == L"customers") ++bare;
        if (t == L"Customers (2)" || t == L"customers (2)") ++suffixed;
    }
    CHECK(bare == 1);
    CHECK(suffixed == 1);

    bool found_collision = false;
    for (const auto& r : result.value().renames) {
        if (r.reason == "collision") found_collision = true;
    }
    CHECK(found_collision);
}

TEST_CASE("collision cascade increments through (2) then (3)", "[folder_mapping]") {
    auto result = build_folder_plan({L"ALPHA", L"Alpha", L"alpha"});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 3);

    std::vector<std::wstring> targets;
    for (const auto& f : result.value().folders) targets.push_back(joined(f.target_segments));

    // All three targets are distinct (never merged).
    for (size_t i = 0; i < targets.size(); ++i) {
        for (size_t j = i + 1; j < targets.size(); ++j) CHECK(targets[i] != targets[j]);
    }
    int with_2 = 0, with_3 = 0;
    for (const auto& t : targets) {
        if (t.find(L" (2)") != std::wstring::npos) ++with_2;
        if (t.find(L" (3)") != std::wstring::npos) ++with_3;
    }
    CHECK(with_2 == 1);
    CHECK(with_3 == 1);
}

TEST_CASE("a genuine folder literally named 'Name (2)' still gets a distinct target on re-collision",
          "[folder_mapping]") {
    // "Name (2)" is a real folder name here, not a generated suffix. Two more
    // case-variant "Name" siblings must still resolve to three distinct
    // targets, exercising the "appending (N) re-collides, increment N" rule.
    auto result = build_folder_plan({L"NAME", L"Name", L"Name (2)"});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 3);

    std::vector<std::wstring> targets;
    for (const auto& f : result.value().folders) targets.push_back(joined(f.target_segments));
    for (size_t i = 0; i < targets.size(); ++i) {
        for (size_t j = i + 1; j < targets.size(); ++j) CHECK(targets[i] != targets[j]);
    }
}

TEST_CASE("Hebrew folder names are preserved verbatim", "[folder_mapping]") {
    std::wstring name = L"תיבת דואר";
    auto result = build_folder_plan({name});
    REQUIRE(result.ok());
    CHECK(joined(result.value().folders[0].target_segments) == name);
    CHECK(result.value().renames.empty());
}

TEST_CASE("exactly 100 path segments is accepted", "[folder_mapping]") {
    std::wstring path;
    for (int i = 1; i <= 100; ++i) {
        if (i > 1) path += L"\\";
        path += L"L" + std::to_wstring(i);
    }
    auto result = build_folder_plan({path});
    CHECK(result.ok());
}

TEST_CASE("101 path segments is rejected", "[folder_mapping]") {
    std::wstring path;
    for (int i = 1; i <= 101; ++i) {
        if (i > 1) path += L"\\";
        path += L"L" + std::to_wstring(i);
    }
    auto result = build_folder_plan({path});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().operation == "build_folder_plan");
}

TEST_CASE("rename records report source path, resulting target path, and reason", "[folder_mapping]") {
    auto result = build_folder_plan({L"Trailing. "});
    REQUIRE(result.ok());
    REQUIRE(result.value().renames.size() == 1);
    const auto& r = result.value().renames[0];
    CHECK(r.source_relative_folder == L"Trailing. ");
    CHECK(r.target_relative_folder == L"Trailing");
    CHECK(r.reason == "trailing-trim");
}

TEST_CASE("Root Messages participates in top-level collision resolution", "[folder_mapping]") {
    // A real top-level source folder that happens to be named exactly like
    // the synthetic root-messages folder must not merge with it.
    auto result = build_folder_plan({L"", kRootMessagesFolder});
    REQUIRE(result.ok());
    REQUIRE(result.value().folders.size() == 2);
    std::vector<std::wstring> targets;
    for (const auto& f : result.value().folders) targets.push_back(joined(f.target_segments));
    CHECK(targets[0] != targets[1]);
}

TEST_CASE("empty input produces an empty plan", "[folder_mapping]") {
    auto result = build_folder_plan({});
    REQUIRE(result.ok());
    CHECK(result.value().folders.empty());
    CHECK(result.value().renames.empty());
}
