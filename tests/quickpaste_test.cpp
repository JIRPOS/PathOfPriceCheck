#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>

#include "config.hpp"
#include "quickpaste.hpp"

using namespace ppc;

// The paste list's model. It is here rather than beside the popup because the popup links ImGui
// and this does not — and because the two things worth pinning are both rules the UI only
// *enforces*: which entries get a number key, and what a multi-line body reads as on one line.

namespace {

std::vector<Paste> list_of(std::initializer_list<bool> enabled) {
    std::vector<Paste> v;
    for (const bool on : enabled) v.push_back(Paste{"h", "b", on});
    return v;
}

} // namespace

TEST_CASE("the popup offers the enabled entries, in list order") {
    const std::vector<Paste> v = list_of({false, true, false, true});
    const std::vector<size_t> active = active_pastes(v);
    REQUIRE(active.size() == 2);
    CHECK(active[0] == 1);
    CHECK(active[1] == 3);
    CHECK(enabled_pastes(v) == 2);
}

TEST_CASE("a tenth enabled paste gets no slot, and the ninth still does") {
    std::vector<Paste> v = list_of({true, true, true, true, true, true, true, true, true, true});
    CHECK(active_pastes(v).size() == kMaxActivePastes);
    // Every slot the popup draws is a key somebody can press, so the last one is the ninth
    // entry and not the tenth.
    CHECK(active_pastes(v).back() == kMaxActivePastes - 1);

    // A hand-edited config is where this arrives from, and loading it turns the extras off
    // rather than drawing keys nobody can press.
    CHECK(limit_enabled(v) == 1);
    CHECK(enabled_pastes(v) == kMaxActivePastes);
    CHECK_FALSE(v.back().enabled);
    CHECK(limit_enabled(v) == 0); // already within the ceiling: nothing to do
}

TEST_CASE("moving an entry shifts the rest along") {
    std::vector<Paste> v;
    for (const char* h : {"a", "b", "c", "d"}) v.push_back(Paste{h, "x", true});

    CHECK(move_paste(v, 3, 0));
    CHECK(v[0].heading == "d");
    CHECK(v[1].heading == "a");
    CHECK(v[3].heading == "c");

    // A drag that has run off the end of the list asks for a move that cannot happen, which is
    // why this answers rather than clamping: the caller redraws the list it already has.
    CHECK_FALSE(move_paste(v, 0, 0));
    CHECK_FALSE(move_paste(v, 0, 4));
    CHECK_FALSE(move_paste(v, 9, 1));
}

TEST_CASE("a body reads as one line") {
    CHECK(paste_preview("  first\n\tsecond   third \n\n") == "first second third");
    CHECK(paste_preview("").empty());
    CHECK(paste_preview("\n \t ").empty());
}

#ifndef _WIN32
TEST_CASE("the list survives a save and a load, ceiling included") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ppc-paste-test";
    std::filesystem::remove_all(dir);
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);

    Config c;
    for (int i = 0; i < 11; ++i)
        c.pastes.push_back(Paste{"heading " + std::to_string(i), "line one\nline two", true});
    c.quick_paste = Hotkey{Mod::Alt, "V"};
    REQUIRE(c.save());

    const Config back = Config::load();
    REQUIRE(back.pastes.size() == 11); // every one is kept — the ceiling is on the *enabled*
    CHECK(back.pastes[0].heading == "heading 0");
    CHECK(back.pastes[0].body == "line one\nline two"); // newlines are the whole point of a body
    CHECK(to_string(back.quick_paste) == "Alt+V");
    // A file claiming eleven active pastes was written by hand; loading it is where that is
    // answered, not the popup.
    CHECK(enabled_pastes(back.pastes) == kMaxActivePastes);
    CHECK_FALSE(back.pastes[9].enabled);
    CHECK_FALSE(back.pastes[10].enabled);

    std::filesystem::remove_all(dir);
}
#endif

TEST_CASE("a body past the budget ends in an ellipsis, never mid-character") {
    CHECK(paste_preview("abcdef", 3) == "abc\xe2\x80\xa6");
    CHECK(paste_preview("abc", 3) == "abc"); // exactly the budget needs no ellipsis
    // Three-byte characters: the cut counts characters, and a half-written UTF-8 sequence is
    // what draws as a box in the popup.
    CHECK(paste_preview("\xe2\x86\x92\xe2\x86\x92\xe2\x86\x92", 2) ==
          "\xe2\x86\x92\xe2\x86\x92\xe2\x80\xa6");
    // The whole point of the budget: a body of any size costs a bounded amount of work.
    CHECK(paste_preview(std::string(100000, 'x'), 10) == "xxxxxxxxxx\xe2\x80\xa6");
}
