#include "ui/strings.hpp"

#include <array>

namespace ppc::ui {
namespace {

constexpr size_t kCount = static_cast<size_t>(Msg::Count);
/// A raw array rather than a `std::array`, so that a table one entry short is a compile
/// error against the `static_assert` below instead of a silently zero-filled tail.
using Table = const char* const*;

/// The English table, and the fallback for every entry another language leaves out. Written
/// in `Msg` order; a designated-initialiser form would survive reordering but C++20 forbids
/// them on an array, so the order here is the contract and `kEnglish` is the one place it is
/// stated.
constexpr const char* kEnglish[]{
    "Path of Price Check \xe2\x80\x94 Settings",
    "Close",

    "General",
    "Price check",
    "QuickPaste",
    "Map check",
    "Application",

    "League and account",
    "Language",
    "Appearance",
    "Trade search",
    "Filter ranges",
    "Hotkeys",
    "Pastes",
    "Price-check panel",
    "Game data",
    "Updates",
    "Diagnostics",

    "League",
    "Refresh",
    "Just refreshed \xe2\x80\x94 wait %ds",
    "Fetching league list\xe2\x80\xa6",
    "%zu leagues",
    "Couldn't reach the trade API (%s)",
    "Offline list",
    "Account",
    "Name#1234",
    "Expected Name#1234",

    "Client language",
    "The language your game client prints item text in. Item text is matched word for word, "
    "so this has to be right or nothing parses.",
    "Interface",
    "Follow the client",
    "Takes effect the next time the application starts.",
    "This bundle carries no wordings for that language, so item text is being read with the "
    "built-in English ones.",

    "Listings",
    "Fetch top",
    "Top %d",
    "%d request%s per check \xe2\x80\x94 about %d checks per 5 minutes",
    "Auto-search",
    "Every price check spends a trade API request.",
    "Off: the panel searches when you press Search.",

    "How wide each modifier's filter opens around the roll in hand.",
    "Minimum",
    "Maximum",
    "Unbound leaves that side open. Tiered never asks past what the modifier's own tier can "
    "roll.",

    "Price check",
    "Settings",
    "QuickPaste",
    "Map check",
    "press keys\xe2\x80\xa6",

    "Reduce transparency",
    "Draws the panels solid instead of letting the game show through them.",

    "Docks beside whichever game panel the cursor was over.",
    "Width",
    "Stash edge",
    "Inventory edge",

    "The QuickPaste hotkey opens this list at your cursor. Picking one puts its text on your "
    "clipboard \xe2\x80\x94 you paste it yourself, where you meant to.",
    "Nothing here yet.",
    "%zu of %zu slots used \xe2\x80\x94 the popup picks by number key.",
    "All %zu slots are taken. Turn one off to give another a number.",
    "(no heading)",
    "(nothing to paste)",
    "New paste",
    "Edit paste",
    "Delete",
    "Drag to reorder",
    "Heading",
    "What it is",
    "Text",
    "What goes on the clipboard. Newlines are kept.",
    "Done",
    "Cancel",
    "Too long to put on the clipboard \xe2\x80\x94 the ceiling is %zu bytes.",
    "No pastes enabled.",
    "Add a paste",

    "Profiles",
    "Modifiers",
    "Profile",
    "No profiles yet",
    "New profile",
    "Name",
    "e.g. Hardcore",
    "Start from",
    "— an empty profile —",
    "Create",
    "Cancel",
    "Delete profile",
    "Delete the profile \"%s\"?",
    "Its %zu ratings go with it, and nothing here can bring them back.",
    "Auto-load",
    "Load this profile automatically for the character you are playing. Not implemented yet.",
    "Load this profile automatically for the character you are playing. Needs reading the "
    "client log, which has to be turned on first.",
    "Search, or paste a map search string",
    "Plain words narrow the list. A search string you already use for the map device works "
    "too — quoted terms, ! for what you refuse.",
    "The same search the game's own stash and map-device boxes take.\n\n"
    "monster damage \xe2\x80\x94 both words, on one modifier\n"
    "\"pack size\" \xe2\x80\x94 quotes hold a term with a space in it\n"
    "!reflect \xe2\x80\x94 hide every modifier saying it\n"
    "\\d+ e \xe2\x80\x94 the terms are real regular expressions\n"
    "ll damage$ \xe2\x80\x94 ^ and $ anchor to a printed line\n"
    "a|b \xe2\x80\x94 either, inside quotes as well as outside\n\n"
    "A modifier matches when any one of its lines does, its affix name included. Terms like "
    "ilvl:84 ask about the item rather than a modifier and are ignored here.",
    "Ignored, as questions about the item and not about a modifier: %s",
    "Rated on a shorter modifier this one contains. Click to decide it here instead.",
    "Propose",
    "Apply the search to every modifier. What a ! term hits is proposed deadly, what a plain "
    "term hits is proposed safe. Nothing in between, and nothing until you accept it.",
    "That search names nothing in the list.",
    "%d deadly, %d safe — shown below, and not saved until you accept.",
    "Accept",
    "%zu of %zu modifiers",
    "The installed data has no modifier pool. Update the game data to fill this list.",
    "Nothing matches that search.",
    "No profile yet",
    "A profile is one table of verdicts. Make one to start rating modifiers.",
    "%zu rated",
    "Map check",
    "Click a modifier to rate it: safe, dangerous, deadly, back to unrated.",
    "This wording is not one the data can identify, so there is nothing to attach a verdict to.",
    "This map has nothing rolled to rate.",
    "You can probably run this map but it has too many unrated modifiers.",
    "You can run this map safely.",
    "You can run this map safely but check the unrated modifiers.",
    "You should be able to run this map.",
    "You should be able to run this map but be careful.",
    "You will most probably die in this map.",

    "Bundle",
    "Downloading %.1f / %.1f MB",
    "Downloading\xe2\x80\xa6",
    "Checking for updates\xe2\x80\xa6",
    "Installing\xe2\x80\xa6",
    "No data installed",
    "Not downloaded yet",
    "Check now",
    "Item parsing works without this; pricing needs it.",
    "%zu stat wordings indexed",
    "Unique modifier data from %s",

    "Application",
    "Up to date",
    "Checking for updates\xe2\x80\xa6",
    "Downloading %.1f / %.1f MB",
    "v%s is ready",
    "v%s is available",
    "This copy cannot update itself \xe2\x80\x94 it is installed somewhere it may not write.",
    "This copy cannot update itself \xe2\x80\x94 it runs from a package or a build tree, which "
    "something other than this application owns.",
    "This release has no download for this platform.",
    "Restart now",
    "Release page",
    "Update automatically",
    "Checks GitHub at startup and while you play, downloads in the background, applies on your "
    "next start.",

    "Debug logging",
    "could not open a log file",
    "Records the copy path, item text included. Off by default.",

    "Save",
    "Click to open the folder",
};

/// Every compiled-in table. English is index 0 and is the fallback, so it is the one entry
/// that may never be removed. A new language is a table added here and nothing else — no
/// build flag, no asset, no second binary.
struct Language {
    std::string_view id;
    Table table;
};
static_assert(std::size(kEnglish) == kCount, "the English table needs one entry per Msg");

constexpr Language kLanguages[]{
    {"en", kEnglish},
};

/// A null entry falls through to English, so a table can be added with only the rows somebody
/// has actually translated filled in.
Table selected = kEnglish;
std::string_view selected_id = "en";

Table table_for(std::string_view lang) {
    for (const Language& l : kLanguages)
        if (l.id == lang) return l.table;
    return nullptr;
}

} // namespace

const char* text(Msg m) {
    const size_t i = static_cast<size_t>(m);
    if (i >= kCount) return "";
    const char* s = selected[i];
    return s && *s ? s : kEnglish[i];
}

void set_language(std::string_view lang, std::string_view client) {
    const std::string_view want = lang.empty() || lang == "auto" ? client : lang;
    if (const Table t = table_for(want)) {
        selected = t;
        selected_id = want;
        return;
    }
    selected = kEnglish;
    selected_id = "en";
}

std::string_view language() { return selected_id; }

std::span<const std::string_view> languages() {
    static const std::array<std::string_view, std::size(kLanguages)> ids = [] {
        std::array<std::string_view, std::size(kLanguages)> out{};
        for (size_t i = 0; i < std::size(kLanguages); ++i) out[i] = kLanguages[i].id;
        return out;
    }();
    return ids;
}

} // namespace ppc::ui
