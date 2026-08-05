#include "screens/pricecheck_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "screens/item_view.hpp"
#include "trade/query.hpp"
#include "util/debug_log.hpp"

namespace ppc {
namespace {

constexpr ImVec4 kDim(0.55f, 0.55f, 0.55f, 1.0f);
constexpr ImVec4 kWarn(0.90f, 0.55f, 0.25f, 1.0f);

/// Narrower than this and an item tooltip wraps every mod onto three lines, which is worse
/// than putting it over the panel. Only reachable on a game window too small to have room
/// beside the panel at all.
constexpr float kMinGutter = 260.0f;

std::string format_number(double v, int dp) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", dp, v);
    return buf;
}

/// "80 to 89", "at least 42", or nothing at all for a mod with no roll to filter on.
std::string bounds_text(const std::optional<double>& min, const std::optional<double>& max,
                        int dp) {
    if (min && max) {
        if (*min == *max) return format_number(*min, dp);
        return format_number(*min, dp) + " to " + format_number(*max, dp);
    }
    if (min) return format_number(*min, dp) + "+";
    // Not "up to": the filter asks for -11 and everything past it, and "up to -11" reads to
    // half the world as the range between zero and -11.
    if (max) return format_number(*max, dp) + " or lower";
    return {};
}

/// The same, in the compact form the stat rows use: "[77-90]" for a bounded search, "42+" for
/// a search that only has a floor.
std::string range_text(const std::optional<double>& min, const std::optional<double>& max,
                       int dp) {
    if (min && max && *min != *max)
        return "[" + format_number(*min, dp) + "-" + format_number(*max, dp) + "]";
    if (min && max) return "[" + format_number(*min, dp) + "]";
    return bounds_text(min, max, dp);
}

/// The trade site's own colours for the two halves of the mod pool, which is where the user
/// reads them everywhere else.
constexpr ImVec4 kPrefix(0.85f, 0.36f, 0.36f, 1.0f);
constexpr ImVec4 kSuffix(0.45f, 0.62f, 0.95f, 1.0f);
constexpr ImVec4 kBounds(0.78f, 0.78f, 0.92f, 1.0f);

struct Code {
    std::string text;
    ImVec4 colour = kBounds;
};

/// Where a modifier came from, as short as it goes: "P2" is a tier-2 prefix, "S1" a tier-1
/// suffix, "R" a crafted one. Empty without Advanced Mod Descriptions, which is the only thing
/// that says which side of the pool a roll is from. A crafted mod is still a prefix or a
/// suffix, so the colour says which even though the letter no longer does.
Code affix_code(const item::Modifier& m) {
    Code c;
    if (m.type == data::ModType::Crafted) c.text = "R";
    else if (m.affix == item::Affix::Prefix) c.text = "P";
    else if (m.affix == item::Affix::Suffix) c.text = "S";
    else return {};
    if (m.affix == item::Affix::Prefix) c.colour = kPrefix;
    else if (m.affix == item::Affix::Suffix) c.colour = kSuffix;
    if (const int n = m.tier ? m.tier : m.rank) c.text += std::to_string(n);
    return c;
}

/// One code per modifier folded into the filter — two life rolls are searched as their total
/// but are still two affixes, and the user is picking which to keep.
std::vector<Code> affix_codes(const item::Item& it, const item::StatFilter& f) {
    std::vector<Code> out;
    if (Code c = affix_code(it.mods[f.mod_index]); !c.text.empty()) out.push_back(std::move(c));
    for (const size_t i : f.merged)
        if (Code c = affix_code(it.mods[i]); !c.text.empty()) out.push_back(std::move(c));
    return out;
}

void draw_strategy_picker(App& app, const item::Item& it, item::SearchPlan& plan) {
    ImGui::TextColored(kDim, "Search as");
    ImGui::SameLine();
    // A rolled item can be worth more as a base than as the sum of its mods — a fractured
    // mod, a good influence, a high item level — and only the user knows which they meant.
    if (it.rarity == item::Rarity::Magic || it.rarity == item::Rarity::Rare) {
        for (const item::Strategy s : {item::Strategy::Modifiers, item::Strategy::BaseItem}) {
            const bool on = plan.strategy == s;
            if (ImGui::RadioButton(std::string(item::to_string(s)).c_str(), on) && !on)
                app.set_strategy(s);
            ImGui::SameLine();
        }
        ImGui::NewLine();
    } else {
        ImGui::TextUnformatted(std::string(item::to_string(plan.strategy)).c_str());
    }

    std::string target;
    if (!plan.name.empty()) target = plan.name;
    if (!plan.type.empty()) target += (target.empty() ? "" : ", ") + plan.type;
    if (!plan.category.empty()) target += (target.empty() ? "" : " — ") + plan.category;
    if (!target.empty()) ImGui::TextColored(kDim, "%s", target.c_str());
}

/// One filter row: a toggle, where the modifier came from, what it asks for, and the wording.
/// The codes and bounds go first because they are the part being compared; the wording wraps
/// under them.
void draw_filter_row(int id, bool& enabled, std::span<const Code> codes, const std::string& bounds,
                     const std::string& text, const std::string& note) {
    ImGui::PushID(id);
    ImGui::Checkbox("", &enabled);
    ImGui::PopID();
    ImGui::SameLine();
    const float indent = ImGui::GetCursorPosX();
    for (const Code& c : codes) {
        ImGui::TextColored(c.colour, "%s", c.text.c_str());
        ImGui::SameLine();
    }
    if (!bounds.empty()) {
        ImGui::TextColored(kBounds, "%s", bounds.c_str());
        ImGui::SameLine();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    if (!note.empty()) {
        ImGui::SetCursorPosX(indent);
        ImGui::TextColored(kDim, "%s", note.c_str());
    }
    ImGui::PopTextWrapPos();
}

void draw_filters(const item::Item& it, item::SearchPlan& plan) {
    for (size_t i = 0; i < plan.numerics.size(); ++i) {
        item::NumericFilter& f = plan.numerics[i];
        draw_filter_row(static_cast<int>(i), f.enabled, {}, bounds_text(f.min, f.max, f.dp),
                        f.label, f.note);
    }
    for (size_t i = 0; i < plan.stats.size(); ++i) {
        item::StatFilter& f = plan.stats[i];
        const std::vector<Code> codes = affix_codes(it, f);
        std::string bounds = range_text(f.min, f.max, f.dp);
        // What the unique itself can roll, which the clipboard only prints with Advanced Mod
        // Descriptions on. A point range says nothing a reader cannot see.
        if (f.unique_min && f.unique_max && *f.unique_min != *f.unique_max)
            bounds += " of " + range_text(f.unique_min, f.unique_max, f.dp);
        std::string note;
        if (f.type != data::ModType::Explicit) note = data::trade_prefix(f.type);
        // Why this one is ticked on a unique whose other modifiers are not: the item picked
        // it out of a pool, so it is what separates this copy from every other.
        if (f.pooled) {
            if (!note.empty()) note += " \xe2\x80\x94 ";
            note += f.pool_hint.empty() ? "one of several possible modifiers" : f.pool_hint;
        }
        draw_filter_row(static_cast<int>(1000 + i), f.enabled, codes, bounds,
                        strip_roll_ranges(f.text), note);
    }
}

/// Where poe.ninja's reference price will go: the going rate for a unique, a gem or a stack of
/// currency, which a stat query cannot give — and which for currency is the *only* answer, since
/// those strategies have no search behind them at all. Drawn as an empty slot rather than left
/// out, so the layout it lands in is the one being looked at now. Deliberately absent on a rare:
/// poe.ninja has no price for one and never will.
void draw_reference_price(const item::SearchPlan& plan) {
    switch (plan.strategy) {
    case item::Strategy::Unique:
    case item::Strategy::Currency:
    case item::Strategy::Gem:
        break;
    default:
        return;
    }
    ImGui::TextDisabled("poe.ninja \xe2\x80\x94 reference pricing is not built yet");
}

/// The Search / Open in browser pair, plus whatever the last search had to say. Both act on
/// the filters as they are ticked right now, so changing one's mind and pressing again is
/// the whole interaction.
void draw_search_controls(App& app) {
    const TradeService& t = app.trade();
    const bool busy = t.state() == TradeState::Searching;
    const bool can = app.can_search();

    ImGui::BeginDisabled(busy || !can);
    if (ImGui::Button("Search", ImVec2(110, 0))) app.start_search();
    ImGui::SameLine();
    if (ImGui::Button("Open in browser", ImVec2(150, 0))) app.open_search_in_browser();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (busy) {
        ImGui::TextDisabled("Searching\xe2\x80\xa6");
    } else if (!can) {
        ImGui::TextDisabled("Nothing to search");
    } else if (t.state() == TradeState::Ok) {
        // The total, not the number fetched: "20 of 4" would be a lie and "20 listings" hides
        // that there are two thousand more.
        ImGui::TextDisabled("%d match%s in %s", t.results().total,
                            t.results().total == 1 ? "" : "es", t.league().c_str());
    }
}

/// A price in the trade site's own form: `5× [icon] Divine Orb`. The symbol is downloaded in
/// the background, so the row reads correctly before it arrives and simply closes up around it
/// after — which is why the currency is named in full rather than left to the picture.
///
/// The whole cell is one hover target. Splitting it was an accident of drawing the icon as its
/// own widget, and a tooltip that appears over the number but not over the orb beside it reads
/// as a bug.
///
/// `fee_tip` is false whenever the row's item popup is up, and then the fee rides in that
/// instead. **Two tooltips in one frame collide** — ImGui gives them one window between them,
/// and `AllowOverlap` means hovering the price cell hovers the row as well, so both fired at
/// once and the item card came out mangled.
void draw_price(App& app, const trade::Listing& l, bool fee_tip) {
    if (!l.priced) {
        ImGui::TextDisabled("no price");
        return;
    }
    const float h = ImGui::GetTextLineHeight();
    ImGui::BeginGroup();
    // A plain lowercase x, not the × the site uses: Fontin has no U+00D7 and draws it as "?".
    // Spaced off the number, or "11x" reads as one token at this size.
    ImGui::TextUnformatted((trade::price_text(l.amount) + " x").c_str());
    if (const uint64_t tex = app.icons().texture(app.trade().currency_image(l.currency))) {
        ImGui::SameLine(0.0f, 3.0f);
        ImGui::Image(tex, ImVec2(h, h));
    }
    ImGui::SameLine(0.0f, 3.0f);
    ImGui::TextUnformatted(app.trade().currency_name(l.currency).c_str());
    ImGui::EndGroup();
    // The gold the trade takes on top, which is the one thing the buyer pays that the row
    // cannot show. No fee, no tooltip: repeating the price back at the cursor says nothing.
    if (fee_tip && l.fee > 0 && ImGui::IsItemHovered())
        ImGui::SetTooltip("Fee: %s gold", trade::gold_text(l.fee).c_str());
}

/// The item the check is about, in the **gutter beside the panel** rather than at the top of it.
/// The panel is a column on a screen that is always wider than it is tall, so the vertical space
/// the item used to take was the scarcest thing in it — at 1080p and below it left the filters
/// and the listings fighting over what was left. Beside the panel it costs nothing but gutter,
/// which is otherwise empty until a row is hovered.
///
/// Returns the y the rest of the gutter is free from, or 0 when there was no room to draw it and
/// the panel has to show the item itself. Width is fixed rather than auto-fit: `draw_item_tooltip`
/// centres every line on `GetContentRegionAvail()`, which in an auto-sizing window is whatever
/// the last frame happened to be.
float draw_item_card(App& app, const item::Item& it) {
    const PanelLayout& lay = app.layout();
    if (lay.tip_w < kMinGutter) return 0.0f;
    ImGui::SetNextWindowPos(ImVec2(lay.tip_x, 0));
    // (w, 0): fixed width, height auto-fit to the item. ImGui clamps the auto-fit to the
    // viewport, so a very long item scrolls inside the card instead of running off the screen.
    ImGui::SetNextWindowSize(ImVec2(lay.tip_w, 0.0f));
    ImGui::Begin("##item_card", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    draw_item_tooltip(it, app.derived(), app.fonts());
    const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    ImGui::End();
    return bottom;
}

/// The seller's item while its row is hovered — the same renderer the panel uses for the item
/// in hand, on an item parsed from the clipboard text the API ships with every listing. Two
/// rows of a price table look alike; the items behind them do not, and on a rare that
/// difference is the whole reason one is cheaper.
///
/// It goes in the **gutter beside the panel** (`App::layout`), under the item in hand — the
/// point of the hover is comparing the two, so the one being compared against stays on screen.
/// Both position and width are set explicitly: `SetNextWindowPos` overrides a tooltip's
/// follow-the-mouse placement, and `SetNextWindowSize` overrides its auto-fit per axis, so
/// `(w, 0)` fixes the width and leaves the height to the item.
///
/// Aligned to the top of its own row, but never above `gutter_top` and never so far down that it
/// runs off the bottom. The bottom clamp uses the height this drew at last frame, which is one
/// frame stale and settles immediately; there is no way to know it before drawing.
void draw_listing_tooltip(App& app, const trade::Listing& l, const ListingItem& li, float row_top,
                          float gutter_top) {
    const PanelLayout& lay = app.layout();
    static float last_h = 0;
    if (lay.tip_w >= kMinGutter) {
        const float max_y = std::max(0.0f, ImGui::GetIO().DisplaySize.y - last_h);
        // Not std::clamp: on a gutter whose card leaves less room than the listing needs, the
        // low bound is above the high one, which clamp is not defined for. The card wins.
        ImGui::SetNextWindowPos(ImVec2(lay.tip_x, std::max(gutter_top, std::min(row_top, max_y))));
        ImGui::SetNextWindowSize(ImVec2(lay.tip_w, 0.0f));
    } else {
        // A game window too narrow to spare a gutter: fall back to the panel's own width and
        // let ImGui place it, which means over the results. Better than not showing the item.
        ImGui::SetNextWindowSize(ImVec2(lay.panel_w, 0.0f));
    }
    ImGui::BeginTooltip();
    draw_item_tooltip(*li.item, li.derived, app.fonts());
    // The gold the trade takes on top of the price, under the seller's own note — which is
    // where the site puts it, and the only place it can go now that a competing tooltip is
    // not an option.
    if (l.fee > 0) {
        const std::string fee = "Fee: " + trade::gold_text(l.fee) + " gold";
        ImGui::Separator();
        const float w = ImGui::CalcTextSize(fee.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f));
        ImGui::TextDisabled("%s", fee.c_str());
    }
    last_h = ImGui::GetWindowSize().y;
    ImGui::EndTooltip();
}

/// The last row of the table, when the search found more than has been fetched. One click is
/// one /fetch request for ten more listings and no search — the hashes came back with the
/// original query. The count is stated because the cost is real: the fetch policy allows
/// fifty in five minutes.
void draw_load_more_row(App& app) {
    const TradeService& t = app.trade();
    if (t.results().hashes.empty() || t.remaining() == 0) return;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::BeginDisabled(!t.can_load_more());
    if (ImGui::SmallButton("Load 10 more")) app.load_more();
    ImGui::EndDisabled();
    ImGui::TableSetColumnIndex(2);
    ImGui::TextDisabled("%zu left", t.remaining());
}

void draw_results(App& app, float gutter_top) {
    const TradeService& t = app.trade();
    if (!t.error().empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", t.error().c_str());
        ImGui::PopTextWrapPos();
    }
    if (t.results().listings.empty()) {
        if (t.state() == TradeState::Ok) ImGui::TextDisabled("Nobody is selling one.");
        return;
    }

    const auto now = static_cast<int64_t>(std::time(nullptr));
    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    // The listings are the last thing in the panel, so they get whatever is left of it. Asked
    // for explicitly rather than by bottom-aligning at height 0: this sits inside a child that
    // can itself scroll, and ImGui's bottom-align is not meaningful in one that does. The floor
    // keeps it a table on a panel too short to have anything left — a scroll region two pixels
    // tall is worse than one that overflows.
    const float avail = ImGui::GetContentRegionAvail().y;
    const float floor_h = ImGui::GetFrameHeight() * 4;
    if (!ImGui::BeginTable("listings", 3, kFlags, ImVec2(0, std::max(avail, floor_h)))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Account", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < t.results().listings.size(); ++i) {
        const trade::Listing& l = t.results().listings[i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // The whole handle, "#1234" included: the digits are what tells two players sharing a
        // name apart, and dropping them made the column look like a name it is not. Drawn in
        // the system face — PoE handles are routinely Cyrillic, Hangul or CJK, and Fontin has
        // not a glyph of any of it.
        //
        // A Selectable rather than text so the *row* is the hover target for the item tooltip
        // and lights up to say so. It spans every column, and AllowOverlap lets the price
        // cell's own hover (the fee) sit on top of it.
        ImGui::PushFont(app.fonts().unicode, 0.0f);
        ImGui::Selectable((l.account + "##row" + std::to_string(i)).c_str(), false,
                          ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowOverlap);
        ImGui::PopFont();
        const bool row_hovered = ImGui::IsItemHovered();
        const float row_top = ImGui::GetItemRectMin().y;
        // Only the row under the cursor is ever parsed — that is what keeps the popup lazy.
        const ListingItem* li = row_hovered ? app.listing_item(i) : nullptr;
        const bool popup = li && li->item;
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", trade::age_text(l.indexed_at, now).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::PushID(static_cast<int>(i));
        draw_price(app, l, /*fee_tip=*/!popup);
        ImGui::PopID();
        // After the cells: the tooltip is a window of its own, and opening it mid-row would
        // leave the table's cursor inside it.
        if (popup) draw_listing_tooltip(app, l, *li, row_top, gutter_top);
    }
    draw_load_more_row(app);
    ImGui::EndTable();
}

/// The id of this price check, which is also what tags its lines in the debug log — so a user
/// reporting "this one hung" has a four-character handle for it. Only drawn when the log is on;
/// clicking copies it, because that is easier than transcribing it into a message.
void draw_debug_footer(App& app) {
    const std::string id = debug::check_id();
    ImGui::Separator();
    ImGui::TextColored(kDim, "debug");
    ImGui::SameLine();
    if (id.empty()) { // PPC_DEV_ITEM: the panel is up without a hotkey press behind it
        ImGui::TextDisabled("logging, no check yet");
        return;
    }
    if (ImGui::SmallButton(("#" + id).c_str())) app.copy_check_id();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy this id to the clipboard");
}

} // namespace

void draw_pricecheck_screen(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    const item::Item* it = app.item();
    // The item itself lives in the gutter beside the panel, so the panel's own column is filters,
    // price and listings — the things that need the height. Drawn before the panel purely for
    // reading order; the two windows never overlap. 0 back means there was no gutter to draw it
    // in and the panel has to.
    const float gutter_top = it ? draw_item_card(app, *it) : 0.0f;
    // The card is opaque UI of ours over the game, not the transient tooltip the gutter used to
    // hold: a click on it must not read as a click away from the panel.
    app.set_card_height(gutter_top);

    // The panel occupies its own slice of the window; the rest is the gutter.
    const PanelLayout& lay = app.layout();
    const float panel_w = lay.panel_w > 0 ? lay.panel_w : io.DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(lay.panel_x, 0));
    ImGui::SetNextWindowSize(ImVec2(panel_w, io.DisplaySize.y));
    ImGui::Begin("Price check", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    ImGui::PushFont(app.fonts().bold, 0.0f);
    ImGui::TextUnformatted("Price check");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 34);
    if (ImGui::Button("X", ImVec2(24, 0))) app.close_overlay();
    ImGui::Separator();

    // Copy the snapshot once: the updater can swap the bundle in from its own thread
    // between two reads, and the second would dangle.
    const std::shared_ptr<data::GameData> gd = app.game_data();
    if (!gd) {
        const data::DataUpdater::Status st = app.data_status();
        if (st.state == data::DataUpdater::State::Failed)
            ImGui::TextColored(kWarn, "Pricing data unavailable (%s)", st.error.c_str());
        else if (st.bytes_total)
            ImGui::TextDisabled("Pricing data is downloading (%.1f / %.1f MB)\xe2\x80\xa6",
                                st.bytes_done / 1e6, st.bytes_total / 1e6);
        else
            ImGui::TextDisabled("Pricing data is downloading\xe2\x80\xa6");
        ImGui::Separator();
    }

    // The body is a child filling everything above the footer, which keeps the footer pinned to
    // the bottom without seeking the cursor past the content.
    const float footer_h =
        debug::enabled() ? ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeight() : 0.0f;
    ImGui::BeginChild("body", ImVec2(0, -footer_h), ImGuiChildFlags_None);
    // The panel is only opened once there *is* an item — a check that copies nothing, or
    // something that isn't an item, is dropped without ever showing. So this has no waiting
    // or failure states to render; the branch is for PPC_DEV_ITEM pointed at a bad capture.
    if (!it) {
        ImGui::TextDisabled("Nothing to show.");
    } else {
        // Only when the game window left no room for a gutter: then the item is still the first
        // thing in the panel, as it was before it had anywhere else to go.
        if (gutter_top == 0.0f) {
            draw_item_tooltip(*it, app.derived(), app.fonts());
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Separator();
        }

        item::SearchPlan& plan = app.plan();
        if (!gd) {
            ImGui::TextDisabled("No pricing data yet, so nothing has been matched.");
        } else {
            draw_strategy_picker(app, *it, plan);
            ImGui::Dummy(ImVec2(0, 4));
            draw_filters(*it, plan);
            // A dropped filter has to be visible: silently searching without it reads as a
            // successful price check on an item that is not this one.
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(kWarn));
            for (const std::string& n : plan.notes)
                ImGui::TextUnformatted(("\xe2\x80\xa2 " + n).c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::Separator();
            draw_reference_price(plan);
            draw_search_controls(app);
            draw_results(app, gutter_top);
        }
    }
    ImGui::EndChild();

    if (footer_h > 0.0f) draw_debug_footer(app);

    ImGui::End();
}

} // namespace ppc
