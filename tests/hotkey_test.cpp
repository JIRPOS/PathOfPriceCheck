#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "platform/input.hpp"

using namespace ppc;

// Price check is Ctrl+D and map check is Ctrl+Shift+D, so one chord is the other with a
// modifier added. Everything that decides which of them fired compares the **whole** modifier
// set, and these are the cases that would break if any of it ever became a subset test.
//
// The comparison the two OS backends make is theirs and cannot be exercised here — both are
// exact by construction: an X11 passive grab activates on the modifier mask it was registered
// with (which is why the lock combinations are enumerated one by one in `hotkeys_x11`), and
// `RegisterHotKey` is exact on its flags. `X11Hotkeys::dispatch` then re-checks `g.mods == st`
// against the event's own state, which is the equality this file is about.

TEST_CASE("a chord is a set of modifiers, and two chords sharing a key are not equal") {
    const Hotkey price = parse_hotkey("Ctrl+D");
    const Hotkey map = parse_hotkey("Ctrl+Shift+D");

    CHECK(price.key == "D");
    CHECK(map.key == "D");
    CHECK(price.mods == Mod::Ctrl);
    CHECK(map.mods == (Mod::Ctrl | Mod::Shift));
    // The whole point: the letter is shared and the chords are not the same thing.
    CHECK(price.mods != map.mods);
    // A subset test is what would fire the price check on the map check's chord.
    CHECK(has(map.mods, Mod::Ctrl));
    CHECK_FALSE(has(price.mods, Mod::Shift));
}

TEST_CASE("a chord of several modifiers round-trips through the config file") {
    for (const char* s : {"Ctrl+D", "Ctrl+Shift+D", "Ctrl+Shift+Alt+F5", "Alt+V", "Shift+Space",
                          "Ctrl+Alt+Super+Home", "F12"})
        CHECK(to_string(parse_hotkey(s)) == s);
}

TEST_CASE("modifiers are written in one order however they were typed") {
    // The file holds what `to_string` wrote, so a hand-edited "Shift+Ctrl+D" has to mean the
    // same binding as the one Settings would have saved.
    CHECK(parse_hotkey("Shift+Ctrl+D").mods == parse_hotkey("Ctrl+Shift+D").mods);
    CHECK(to_string(parse_hotkey("Shift+Ctrl+D")) == "Ctrl+Shift+D");
}

TEST_CASE("an unbound hotkey is one nothing is registered for") {
    CHECK_FALSE(Hotkey{}.valid());
    CHECK_FALSE(parse_hotkey("").valid());
    // Modifiers with no key are not a binding either — there is nothing to grab.
    CHECK_FALSE(parse_hotkey("Ctrl+Shift+").valid());
    CHECK(parse_hotkey("Ctrl+Shift+D").valid());
}
