#pragma once

#include <string>
#include <string_view>

#include "item/range_match.hpp"
#include "platform/input.hpp"
#include "trade/trade.hpp"

namespace ppc {

enum class NameCheck { Empty, Ok, Malformed };

/// PoE account handles are "Name#1234": an alphanumeric name, '#', then digits.
/// Empty is valid — the field is optional.
NameCheck check_account_name(std::string_view s);

struct Config {
    std::string league = "Standard";
    std::string account_name; ///< optional; "Name#1234", see check_account_name

    /// Which language the *client* is in, as the data bundle names it. This picks the assets
    /// the bundle is opened with and the vocabulary the clipboard is read against — not the
    /// language this application's own text is drawn in, which is `ui_language`.
    ///
    /// Not validated on load: the set of languages is whatever the installed bundle declares,
    /// and a bundle that does not carry this one simply fails to open, which the updater
    /// already reports. Defaults to English, which is what every bundle has.
    std::string client_language = "en";

    /// Which language this application's own text is drawn in — "auto" to follow the client.
    /// Separate from `client_language` because they answer to different things: one has to
    /// match the game the user is running, the other the person reading the panel.
    std::string ui_language = "auto";

    /// Substring matched against the foreground window title. Deliberately not in the
    /// Settings UI — it only needs changing when Wine/Lutris renames the window, which is
    /// rare enough to be worth a config-file edit rather than a knob everyone else scrolls past.
    std::string poe_window_title = "Path of Exile";

    Hotkey price_check{Mod::Ctrl, "D"};
    Hotkey settings{Mod::Shift, "Space"};

    /// Run the trade search as soon as the panel opens, rather than on the Search button.
    /// Off by default and deliberately so: a hotkey the user pressed to *read* an item
    /// would otherwise spend a request against their rate limit every time.
    bool auto_search = false;

    /// Which listings a search asks for — one of `trade::status_options()`. Instant Buyout
    /// by default: an offer that can be taken without the seller being at their keyboard is
    /// what most people now mean by "for sale". Validated on load against that closed list,
    /// unlike `league`, because a value GGG does not know makes every search fail outright.
    std::string listing_status{trade::kDefaultStatus};

    /// How wide each modifier's filter is seeded around the roll in hand — the tier-gated 5%
    /// window by default. See item/range_match.hpp for what each mode means.
    item::RangeMatch range_match;

    /// How many listings a search pulls — one of `trade::result_counts()`. Every ten is one
    /// more fetch request against a policy that allows 50 per five minutes, so this is a
    /// choice about how many price checks fit in that window, not about latency.
    int result_count = trade::kDefaultResultCount;

    // Price-check panel placement. The game's UI scales with window *height*, so the stash
    // and inventory frames sit at a fixed fraction of it regardless of aspect ratio. The two
    // are mirror images — tuned by hand they came out at 0.617 and 0.616 — but they stay
    // separate knobs because GGG moves these between leagues. Eyeballing them off a
    // screenshot is not accurate to the pixel; adjust in Settings against the live game.
    int panel_width = 460;         ///< price-check panel width, px
    float stash_edge = 0.615f;     ///< stash frame's right edge, ÷ game height
    float inventory_edge = 0.615f; ///< inventory frame's left edge from the right, ÷ game height

    // Where the idle status sits: the middle of the mana globe, which is anchored to the
    // bottom-right corner of the game window and scales with its height — so both offsets are
    // measured from that corner and divided by the height, like the two edges above. Defaults
    // are off tests/data/full_screen_1440p.jpeg. Config-file only, like poe_window_title: the
    // globe moves when GGG moves the HUD, which is not often enough to be a knob in the UI.
    float status_right = 0.098f;  ///< globe centre's distance from the right edge, ÷ game height
    float status_bottom = 0.080f; ///< globe centre's distance from the bottom edge, ÷ game height

    /// Write the copy-path diagnostic log (see util/debug_log). Off for everyone by default:
    /// it records the whole clipboard, item text included.
    bool debug_log = false;

    static std::string path(); ///< platform config-file path
    static Config load();      ///< load from disk, or defaults if absent/invalid
    bool save() const;
};

} // namespace ppc
