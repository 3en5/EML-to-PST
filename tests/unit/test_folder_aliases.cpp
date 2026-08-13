// WLM2PST - unit tests for worker/folder_mapping/folder_aliases (spec
// sections 12, 28).
#include "worker/folder_mapping/folder_aliases.h"

#include <catch2/catch_test_macros.hpp>

using namespace wlm2pst;

TEST_CASE("every documented sent alias is recognized", "[folder_aliases]") {
    CHECK(is_sent_folder_component(L"Sent"));
    CHECK(is_sent_folder_component(L"Sent Items"));
    CHECK(is_sent_folder_component(L"Sent Mail"));
    CHECK(is_sent_folder_component(L"Sent Messages"));
    CHECK(is_sent_folder_component(L"Items Sent"));
    CHECK(is_sent_folder_component(L"פריטים שנשלחו"));
    CHECK(is_sent_folder_component(L"דואר שנשלח"));
    CHECK(is_sent_folder_component(L"נשלח"));
}

TEST_CASE("every documented drafts alias is recognized", "[folder_aliases]") {
    CHECK(is_drafts_folder_component(L"Drafts"));
    CHECK(is_drafts_folder_component(L"Draft"));
    CHECK(is_drafts_folder_component(L"טיוטות"));
    CHECK(is_drafts_folder_component(L"טיוטה"));
}

TEST_CASE("sent alias matching is case-insensitive", "[folder_aliases]") {
    CHECK(is_sent_folder_component(L"SENT"));
    CHECK(is_sent_folder_component(L"sent"));
    CHECK(is_sent_folder_component(L"SeNt ItEmS"));
    CHECK(is_sent_folder_component(L"sent mail"));
    CHECK(is_sent_folder_component(L"ITEMS SENT"));
}

TEST_CASE("drafts alias matching is case-insensitive", "[folder_aliases]") {
    CHECK(is_drafts_folder_component(L"DRAFTS"));
    CHECK(is_drafts_folder_component(L"draft"));
    CHECK(is_drafts_folder_component(L"DrAfTs"));
}

TEST_CASE("alias matching is exact component match, not substring", "[folder_aliases]") {
    CHECK_FALSE(is_sent_folder_component(L"Sentinel"));
    CHECK_FALSE(is_sent_folder_component(L"Unsent"));
    CHECK_FALSE(is_sent_folder_component(L"Sent2"));
    CHECK_FALSE(is_sent_folder_component(L" Sent"));
    CHECK_FALSE(is_drafts_folder_component(L"Drafted"));
    CHECK_FALSE(is_drafts_folder_component(L"MyDrafts"));
    CHECK_FALSE(is_drafts_folder_component(L"Draftsman"));
}

TEST_CASE("unrelated and empty names are not aliases", "[folder_aliases]") {
    CHECK_FALSE(is_sent_folder_component(L"Inbox"));
    CHECK_FALSE(is_drafts_folder_component(L"Inbox"));
    CHECK_FALSE(is_sent_folder_component(L""));
    CHECK_FALSE(is_drafts_folder_component(L""));
}

TEST_CASE("path_has_sent_component finds the alias at any position", "[folder_aliases]") {
    CHECK(path_has_sent_component(L"Sent"));
    CHECK(path_has_sent_component(L"account@example.com\\Sent Items"));
    CHECK(path_has_sent_component(L"Sent Items\\2020"));
    CHECK(path_has_sent_component(L"account@example.com\\Sent Items\\2020"));
    CHECK_FALSE(path_has_sent_component(L"account@example.com\\Inbox"));
    CHECK_FALSE(path_has_sent_component(L"account@example.com\\Sentinel\\2020"));
}

TEST_CASE("path_has_drafts_component finds the alias at any position", "[folder_aliases]") {
    CHECK(path_has_drafts_component(L"Drafts"));
    CHECK(path_has_drafts_component(L"account@example.com\\Drafts"));
    CHECK(path_has_drafts_component(L"Drafts\\Old"));
    CHECK_FALSE(path_has_drafts_component(L"account@example.com\\Inbox"));
    CHECK_FALSE(path_has_drafts_component(L"account@example.com\\MyDrafts"));
}

TEST_CASE("path-level Hebrew alias detection", "[folder_aliases]") {
    CHECK(path_has_sent_component(L"תיבת דואר\\נשלח"));
    CHECK(path_has_drafts_component(L"תיבת דואר\\טיוטות"));
    CHECK_FALSE(path_has_sent_component(L"תיבת דואר\\דואר נכנס"));
}

TEST_CASE("a folder is not both sent and drafts", "[folder_aliases]") {
    CHECK_FALSE(is_drafts_folder_component(L"Sent"));
    CHECK_FALSE(is_sent_folder_component(L"Drafts"));
}
