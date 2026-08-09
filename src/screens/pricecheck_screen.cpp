#include "screens/pricecheck_screen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "exchange/exchange.hpp"
#include "ninja/ninja.hpp"
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

/// The listing that is the user's own. Translucent, and set as `RowBg1` rather than `RowBg0`,
/// so it tints the alternating stripe underneath instead of replacing it — the row stays part
/// of the table, and the hover highlight still reads on top of it.
constexpr ImU32 kOwnRow = IM_COL32(60, 140, 70, 90);

/// Whether a listing is the user's own. Case-insensitive: the handle is typed into Settings by
/// hand, and one entered with the wrong capital would simply never light up — a failure with
/// nothing on screen to show for it. ASCII is the whole of it by construction, since
/// `check_account_name` and the Settings field both refuse anything else.
bool same_account(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) ==
               std::tolower(static_cast<unsigned char>(y));
    });
}

std::string format_number(double v, int dp) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", dp, v);
    return buf;
}

/// What the search asks for, in the column it shares with every other filter's answer to the
/// same question: two bounds as `46-48`, a floor as `≥46`, a ceiling as `≤50`, a point as a
/// bare number, and nothing at all for a filter that asks only that the modifier be present.
///
/// The real glyphs are **borrowed from a system face** — Fontin's own are empty outlines, so
/// they used to paint nothing at all (see `kBorrowedGlyphs`). `glyphs` is false only where the
/// OS shipped nothing to borrow from, and then they are spelled out: a floor whose `≥` went
/// missing reads as an exact match, which is a different search.
std::string filter_text(const std::optional<double>& min, const std::optional<double>& max, int dp,
                        bool glyphs) {
    if (min && max) {
        if (*min == *max) return format_number(*min, dp);
        return format_number(*min, dp) + "-" + format_number(*max, dp);
    }
    if (min) return (glyphs ? "\xe2\x89\xa5" : ">=") + format_number(*min, dp);
    if (max) return (glyphs ? "\xe2\x89\xa4" : "<=") + format_number(*max, dp);
    return {};
}

/// What the modifier *can* roll, beside where it came from: "[77-90]". Empty unless both ends
/// are known, since half a range says nothing the filter column does not already say.
std::string bracket_text(const std::optional<double>& min, const std::optional<double>& max,
                         int dp) {
    if (!min || !max) return {};
    if (*min == *max) return "[" + format_number(*min, dp) + "]";
    return "[" + format_number(*min, dp) + "-" + format_number(*max, dp) + "]";
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

/// The short name for a modifier that is not a rolled affix — the ones the game prints in
/// their own colour rather than in the explicit blue. `data::trade_prefix` is the long form
/// and does not fit a column four characters wide.
std::string_view type_code(data::ModType t) {
    switch (t) {
        case data::ModType::Implicit: return "Impl";
        case data::ModType::Enchant: return "Ench";
        case data::ModType::Fractured: return "Frac";
        case data::ModType::Veiled: return "Veil";
        case data::ModType::Scourge: return "Scrg";
        case data::ModType::Crucible: return "Cruc";
        default: return {};
    }
}

/// Where a modifier came from, as short as it goes: "P2" is a tier-2 prefix, "S1" a tier-1
/// suffix, "R" a crafted one, "Impl" an implicit, "Frac2" a tier-2 fractured affix. Empty for
/// an ordinary roll on an item whose owner has Advanced Mod Descriptions off, which is the only
/// thing that says which side of the pool a roll came from.
///
/// **The colour is the side of the pool and the letters are what put the modifier there**, so
/// the two never compete for the same four characters: a fractured prefix is a red "Frac", and
/// what a buyer needs to know about it first is that it is fractured.
Code affix_code(const item::Modifier& m) {
    Code c;
    if (m.affix == item::Affix::Prefix) c.colour = kPrefix;
    else if (m.affix == item::Affix::Suffix) c.colour = kSuffix;
    if (m.type == data::ModType::Crafted) c.text = "R";
    else if (const std::string_view t = type_code(m.type); !t.empty()) c.text = t;
    else if (m.affix == item::Affix::Prefix) c.text = "P";
    else if (m.affix == item::Affix::Suffix) c.text = "S";
    else return {};
    if (const int n = m.tier ? m.tier : m.rank) c.text += std::to_string(n);
    return c;
}

/// The origin column: where the filter's modifiers came from and what they can roll. One code
/// per modifier `merge_same_stat` folded in — two life rolls are searched as their total but
/// are still two affixes, and the user is picking which to keep.
struct Origin {
    std::vector<Code> codes;
    /// An eldritch implicit's rank, "Lesser" / "Grand" / …, which is the only way that kind of
    /// modifier states its magnitude: it comes from the tier of the currency that applied it,
    /// so the clipboard has no range to print instead.
    std::string qualifier;
    std::string range; ///< "[77-90]", what the modifier can roll
};

Origin origin_of(const item::Item& it, const item::StatFilter& f) {
    Origin o;
    o.range = bracket_text(f.roll_min, f.roll_max, f.dp);
    // A pseudo total has no modifier behind it to have come from a side of the pool.
    if (!f.mod_index) return o;
    const item::Modifier& m = it.mods[*f.mod_index];
    o.qualifier = m.qualifier;
    if (Code c = affix_code(m); !c.text.empty()) o.codes.push_back(std::move(c));
    for (const size_t i : f.merged)
        if (Code c = affix_code(it.mods[i]); !c.text.empty()) o.codes.push_back(std::move(c));
    return o;
}

/// The origin column. The code and the range are glued together — `P2[77-90]` — because they
/// are one fact about one affix. A filter two affixes were folded into writes them as `P3+P1`
/// and drops the range to a line of its own, since it is then the pair's total and belongs to
/// neither code on its own.
void draw_origin(const Origin& o) {
    bool anything = false;
    for (size_t i = 0; i < o.codes.size(); ++i) {
        if (i) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextDisabled("+");
            ImGui::SameLine(0.0f, 0.0f);
        }
        ImGui::TextColored(o.codes[i].colour, "%s", o.codes[i].text.c_str());
        anything = true;
    }
    // The rank goes below the code rather than beside it: the column is as wide as its widest
    // row, and "Impl Lesser" on one line would set that width for every modifier in the list.
    if (!o.qualifier.empty()) {
        ImGui::TextDisabled("%s", o.qualifier.c_str());
        anything = true;
    }
    if (o.range.empty()) return;
    if (anything && o.codes.size() <= 1) ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(kBounds, "%s", o.range.c_str());
}

void draw_strategy_picker(App& app, const item::Item& it, item::SearchPlan& plan) {
    ImGui::TextColored(kDim, "Search as");
    ImGui::SameLine();
    // A rolled item can be worth more as a base than as the sum of its mods — a fractured
    // mod, a good influence, a high item level — and only the user knows which they meant.
    // Not a map or a chart: neither reading is what either is bought for, and the strategy the
    // two share covers every rarity they print.
    if (!it.is_map() && !it.is_chart() &&
        (it.rarity == item::Rarity::Magic || it.rarity == item::Rarity::Rare)) {
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
    // The name in that line is the one thing about an unidentified unique nobody read off the
    // item, so there has to be a way back to the choice — including out of the one this took
    // for itself when the base had a single unique and it turns out to be a bundle behind.
    if (it.unique_entry && it.unique_candidates.size() > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("change")) app.set_unique(nullptr);
    }
}

/// One filter row, across the table's four columns: the toggle, the wording, where the modifier
/// came from and what it can roll, and what the search asks for.
///
/// The wording comes **second**, straight after the tick, because it is the only column every
/// row has something to put in: a pseudo total has no affix behind it and a roll on an item with
/// Advanced Mod Descriptions off has no code, and a gap between the tick and the text reads as a
/// missing checkbox rather than as a modifier with nothing to say about where it came from.
void draw_filter_row(int id, bool& enabled, const Origin& o, const std::string& text,
                     const std::string& note, const std::string& asks,
                     const std::string& caveat = {}) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(id);
    // A box the height of a line of text rather than of a framed widget. This is a list of
    // modifiers with a tick beside each, and at the default frame padding the tick is taller
    // than the wording it belongs to and sets the row pitch for the whole list.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::Checkbox("", &enabled);
    ImGui::PopStyleVar();
    ImGui::PopID();

    ImGui::TableSetColumnIndex(1);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    // Why this row is not ticked, on the row itself. A line under every such modifier is what
    // this replaced, and it repeated a wording already on screen: four of them filled half the
    // panel on a Triad Grip. The tick and the wording are the statement; this is the reason.
    if (!caveat.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", caveat.c_str());
    if (!note.empty()) ImGui::TextColored(kDim, "%s", note.c_str());
    ImGui::PopTextWrapPos();

    ImGui::TableSetColumnIndex(2);
    draw_origin(o);

    ImGui::TableSetColumnIndex(3);
    if (!asks.empty()) ImGui::TextColored(kBounds, "%s", asks.c_str());
}

/// The filter list, as a table so that every row's numbers sit under the previous row's. What
/// the search asks for is the **last** column and not part of the origin beside the code: it is
/// the one thing here that the user will be editing.
///
/// The wording takes the stretch column and everything else fits its content, so the two number
/// columns are as narrow as the widest row needs and the modifier gets the rest.
void draw_filters(const item::Item& it, item::SearchPlan& plan, bool glyphs) {
    const ImGuiStyle& style = ImGui::GetStyle();
    // Tight rows: a filter list is read down the column, and the default spacing puts half a
    // line of nothing between one modifier and the next.
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f));
    // Banded rather than ruled off: a modifier can wrap onto three lines and its origin onto
    // two, so what a reader needs is to see where one row ends, and a separator between every
    // pair of them would cost a line of height per filter to say it.
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingFixedFit |
                                       ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("filters", 4, kFlags)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);

        // The options a plan does *not* show are the ordinary answers — uncorrupted, unmirrored,
        // identified — and they are imposed silently rather than spending three rows saying
        // that nothing is unusual about the item. See `item::OptionFilter`.
        for (size_t i = 0; i < plan.options.size(); ++i) {
            item::OptionFilter& f = plan.options[i];
            if (!f.shown) continue;
            draw_filter_row(static_cast<int>(2000 + i), f.enabled, {}, f.label, {}, f.display);
        }
        for (size_t i = 0; i < plan.numerics.size(); ++i) {
            item::NumericFilter& f = plan.numerics[i];
            draw_filter_row(static_cast<int>(i), f.enabled, {}, f.label, f.note,
                            filter_text(f.min, f.max, f.dp, glyphs));
        }
        for (size_t i = 0; i < plan.stats.size(); ++i) {
            item::StatFilter& f = plan.stats[i];
            // Why this one is ticked on a unique whose other modifiers are not: the item picked
            // it out of a pool, so it is what separates this copy from every other.
            std::string note;
            if (f.pooled)
                note = f.pool_hint.empty() ? "one of several possible modifiers" : f.pool_hint;
            // "absent" goes in the column that says what the search asks for, because that is
            // the whole difference: the row is otherwise identical to one asking for the
            // modifier, and a tick beside a wording the item does not have reads backwards.
            draw_filter_row(static_cast<int>(1000 + i), f.enabled, origin_of(it, f),
                            strip_roll_ranges(f.text), note,
                            f.negated ? "absent" : filter_text(f.min, f.max, f.dp, glyphs),
                            f.caveat);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
}

constexpr ImVec4 kUp(0.40f, 0.78f, 0.42f, 1.0f);
constexpr ImVec4 kDown(0.85f, 0.36f, 0.36f, 1.0f);
/// Wide enough to read a week's shape at a glance, narrow enough to leave the price column
/// most of a 460px panel.
constexpr float kSparkW = 58.0f;

/// poe.ninja's seven daily samples, drawn by hand rather than with `PlotLines`: that one
/// insists on a frame and a background, and this is one line on the row it belongs to.
/// Coloured by where the week ended, which is the same thing the percentage beside it says.
void draw_spark(const ninja::Spark& s, float w, float h) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    if (s.data.size() < 2) return;

    float low = s.data[0], high = s.data[0];
    for (const float v : s.data) {
        low = std::min(low, v);
        high = std::max(high, v);
    }
    // A flat week is a straight line through the middle, not a divide by zero.
    const float span = high - low > 0.01f ? high - low : 1.0f;
    std::vector<ImVec2> pts;
    pts.reserve(s.data.size());
    for (size_t i = 0; i < s.data.size(); ++i) {
        const float x = at.x + w * static_cast<float>(i) / static_cast<float>(s.data.size() - 1);
        pts.push_back(ImVec2(x, at.y + h - (s.data[i] - low) / span * h));
    }
    ImGui::GetWindowDrawList()->AddPolyline(pts.data(), static_cast<int>(pts.size()),
                                            ImGui::GetColorU32(s.change >= 0 ? kUp : kDown),
                                            ImDrawFlags_None, 1.5f);
}

/// A symbol and the name beside it, drawn inline after whatever came before. The picture is
/// allowed not to have arrived — `IconCache::texture` answers "not yet" and the row closes up
/// around it when it lands — which is why the thing is always *named* as well as pictured.
void draw_symbol(App& app, const std::string& icon, const std::string& name) {
    if (const uint64_t tex = app.icons().texture(icon)) {
        const float h = ImGui::GetTextLineHeight();
        ImGui::SameLine(0.0f, 3.0f);
        ImGui::Image(tex, ImVec2(h, h));
    }
    if (name.empty()) return; // the glyph is the whole of it; see `draw_side`
    ImGui::SameLine(0.0f, 3.0f);
    ImGui::TextUnformatted(name.c_str());
}

/// The symbol and full name of a trade currency id. The symbol comes from the trade static
/// data where that has been fetched and from poe.ninja's own payload where it has not — a
/// reference price should not be the one thing that needs a trade request to render.
void draw_currency(App& app, const std::string& id) {
    const NinjaService& n = app.ninja();
    std::string icon = app.trade().currency_image(id);
    if (icon.empty()) icon = n.currency_icon(id);
    std::string name = app.trade().currency_name(id);
    if (name == id) name = n.currency_name(id);
    draw_symbol(app, icon, name);
}

/// The symbol for something the exchange trades that is **not** one of the two denominators —
/// a scarab, an ember, an essence. It has no trade currency id to be found by: the feed states
/// it as a `Metadata/Items/...` path and nothing else on either source is keyed that way. Its
/// display name is the join, and both sources are asked in the same order as everywhere else.
/// Empty is a fine answer; the name alone still reads.
std::string icon_for_item(App& app, const std::string& name) {
    std::string icon = app.trade().image_for_name(name);
    if (icon.empty()) icon = app.ninja().icon_for_name(name);
    return icon;
}

/// How tall one candidate is in the list, and how big its art is in the grid. A row is a
/// picture with the name beside it; the grid is pictures alone, so it can afford a larger one.
constexpr float kUniqueRowArt = 34.0f;
constexpr float kUniqueGridArt = 46.0f;

/// A unique's own artwork, fitted into a `box`-sided square without being stretched.
///
/// **Straight from GGG's CDN**, at the path the data bundle carries for the unique
/// (`BaseType::art`) — the same picture the game draws, and no third party between the two.
/// The **base's inventory footprint** is both the request's size and the aspect to draw at: a
/// body armour is 2×3, and squashing that into a square is what makes two candidates hard to
/// tell apart at this size. It comes from the base rather than the unique because a unique is
/// not a base type in the game's data and carries no size of its own.
///
/// The picture is downloaded in the background like every other symbol on the panel, so it is
/// allowed not to have arrived — and 110 uniques have no path at all, as does every bundle
/// published before the field existed. So the name is never left to the picture: false says
/// there was none, and the caller puts the name in the space the art would have taken.
bool draw_unique_art(App& app, const data::BaseType* u, const data::BaseType* base, float box) {
    const int cw = base && base->w > 0 ? base->w : 0;
    const int ch = base && base->h > 0 ? base->h : 0;
    const std::string url = data::item_image_url(u->art, cw, ch);
    const uint64_t tex = url.empty() ? 0 : app.icons().texture(url);
    if (!tex) return false;
    const float w = cw > 0 ? static_cast<float>(cw) : 1.0f;
    const float h = ch > 0 ? static_cast<float>(ch) : 1.0f;
    const float scale = box / std::max(w, h);
    ImGui::Image(tex, ImVec2(w * scale, h * scale));
    return true;
}

/// Which unique an **unidentified** one is: the one thing its clipboard text cannot say, and
/// the whole of what a unique is bought for. The bundle knows which uniques drop on the base,
/// and where that is more than one — a Prismatic Jewel is fifty different items — only the
/// player looking at the art in their stash can tell, so this asks them rather than guessing.
///
/// **Two shapes, and the list's own height picks which.** One per row, art and name, is what a
/// reader scans; but fifty of those would push the prices and the item itself off the panel, so
/// past half the panel's height it becomes a grid of artwork alone — which is how the item is
/// recognised in the stash anyway — with the name on hover.
///
/// The row is a `Selectable` with the art drawn on top of it, the same shape the poe.ninja row
/// uses: the whole strip is one target, and `AllowOverlap` is what lets the picture sit over it.
void draw_unique_choice(App& app, const item::Item& it) {
    const std::vector<const data::BaseType*>& us = it.unique_candidates;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kWarn, "Unidentified \xe2\x80\x94 which unique is it?");
    ImGui::PopTextWrapPos();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float row_h = kUniqueRowArt + style.ItemSpacing.y;
    // Measured against what is left of the panel here rather than against the whole of it: the
    // half this may take is half of the room the prices and the listings have to share.
    if (static_cast<float>(us.size()) * row_h <= ImGui::GetContentRegionAvail().y * 0.5f) {
        for (size_t i = 0; i < us.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 at = ImGui::GetCursorPos();
            if (ImGui::Selectable("##pick", false, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, kUniqueRowArt)))
                app.set_unique(us[i]);
            ImGui::SetCursorPos(at);
            if (draw_unique_art(app, us[i], it.base, kUniqueRowArt)) ImGui::SameLine(0.0f, 6.0f);
            // Centred against the art rather than sat on its top edge, which is where a line of
            // text lands in a row twice its height.
            ImGui::SetCursorPosY(at.y + (kUniqueRowArt - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextUnformatted(us[i]->name.c_str());
            // The cursor was moved back over the row to draw on top of it, so the row has to
            // close itself: the art is shorter than the strip and the next one would start
            // inside this one.
            ImGui::SetCursorPos(at);
            ImGui::Dummy(ImVec2(0, kUniqueRowArt));
            ImGui::PopID();
        }
        return;
    }

    // A grid, wrapped by hand: ImGui has no flow layout, and the count here runs to fifty.
    const float step = kUniqueGridArt + style.ItemSpacing.x;
    const auto per_row = std::max(
        1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / step));
    for (size_t i = 0; i < us.size(); ++i) {
        if (i % static_cast<size_t>(per_row) != 0) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 at = ImGui::GetCursorPos();
        if (ImGui::Selectable("##pick", false, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(kUniqueGridArt, kUniqueGridArt)))
            app.set_unique(us[i]);
        // Taken here and not after the art: the picture on top of the row claims the hover for
        // itself, and the tooltip belongs to the thing that is clickable.
        const bool hovered = ImGui::IsItemHovered();
        ImGui::SetCursorPos(at);
        if (!draw_unique_art(app, us[i], it.base, kUniqueGridArt)) {
            // No art for this one, and an empty square is a cell nobody can tell from the next
            // empty square. The name goes in the box instead — wrapped to it, and **clipped**
            // to it, since a name three lines long would otherwise be drawn straight over the
            // row underneath. The tooltip is what the rest of it is read from.
            const ImVec2 tl = ImGui::GetCursorScreenPos();
            ImGui::PushClipRect(tl, ImVec2(tl.x + kUniqueGridArt, tl.y + kUniqueGridArt), true);
            ImGui::PushTextWrapPos(at.x + kUniqueGridArt);
            ImGui::TextDisabled("%s", us[i]->name.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopClipRect();
        }
        // Every cell ends the same size whatever it managed to draw, or a row of 2×1 art sets
        // a different pitch from the row of 2×3 art under it.
        ImGui::SetCursorPos(at);
        ImGui::Dummy(ImVec2(kUniqueGridArt, kUniqueGridArt));
        // The name is a tooltip rather than a caption: at this size there is room for the
        // picture or for the words, and the picture is what the item is recognised by.
        if (hovered) ImGui::SetTooltip("%s", us[i]->name.c_str());
        ImGui::PopID();
    }
}

/// A poe.ninja price in the trade site's own form, so the two prices on screen read alike:
/// `5 x [icon] Divine Orb`. `per` turns it from a price into a rate — `201 x [icon] Chaos Orb
/// per [icon] Divine Orb`, which is the only thing the two orbs the market is denominated in
/// can be checked for.
///
/// A stack adds what all of it is worth, **on a line of its own**: the unit price is what says
/// whether a pile is worth moving and the total is what it sells for, neither substitutes for
/// the other, and the two of them together do not fit the width the panel has. The second line
/// names its currency whether or not it is the first's — six thousand chaos is a number said
/// in divine, and a line that reads `6000 x = 29.8 x` with the unit only on the row above is
/// the one thing worse than wrapping.
void draw_ninja_price(App& app, const ninja::Reference& r, bool ambiguous) {
    std::string amount = trade::price_text(r.price.amount);
    if (ambiguous) amount += " \xe2\x80\x93 " + trade::price_text(r.high.amount);
    ImGui::TextUnformatted((amount + " x").c_str());
    draw_currency(app, r.price.currency);
    if (!r.per.empty()) {
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("per");
        draw_currency(app, r.per);
    }
    if (r.stack <= 1) return;
    if (r.per.empty()) {
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("each");
    }
    // "x" and not "×": Fontin has no multiplication sign and draws it as a box.
    ImGui::TextDisabled("x%d =", r.stack);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted((trade::price_text(r.stack_price.amount) + " x").c_str());
    draw_currency(app, r.stack_price.currency);
}

/// What poe.ninja says the item goes for — the going rate for a unique, a gem or a stack of
/// currency, which a stat query cannot give and which for currency is the *only* answer, since
/// those strategies have no search behind them at all. Deliberately absent on a rare: poe.ninja
/// has no price for one and never will.
///
/// Three columns, no header: the source, the price, and the week behind it. The whole row is
/// one click-through to the item's own page, which is where the variants, the history and the
/// listings actually are — everything this row has to leave out.
///
/// False when it drew nothing at all, so the caller knows there is no section to rule off.
bool draw_reference_price(App& app) {
    switch (app.plan().strategy) {
    case item::Strategy::Unique:
    case item::Strategy::Currency:
    case item::Strategy::Gem:
    case item::Strategy::BaseItem:
    case item::Strategy::Modifiers:
        break;
    default:
        return false; // a map: poe.ninja prices nothing about it that this row could show
    }
    const NinjaService& n = app.ninja();
    const ninja::Reference& r = n.reference();

    if (n.state() == NinjaState::Loading) {
        ImGui::TextDisabled("poe.ninja\xe2\x80\xa6");
        return true;
    }
    if (r.state == ninja::Reference::State::None) {
        // Never silent: an empty slot where a price belongs reads as a price of nothing. Wrapped
        // — these say *why* there is none, and a sentence cut off at the panel edge does not.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("poe.ninja \xe2\x80\x94 %s",
                            !n.error().empty()  ? n.error().c_str()
                            : !r.note.empty()   ? r.note.c_str()
                                                : "no reference price");
        ImGui::PopTextWrapPos();
        return true;
    }

    const bool ambiguous = r.state == ninja::Reference::State::Ambiguous;
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("reference", 3, kFlags)) return false;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    const float line_h = ImGui::GetTextLineHeight();
    // The row is the click target, and it has to be laid down before the cells so they draw
    // on top of it. AllowOverlap is what lets the hover fall through to it.
    if (ImGui::Selectable("##ninja_row", false,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(0, line_h)))
        app.open_reference_page();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::SameLine(0.0f, 0.0f);
    if (const uint64_t tex = app.icons().texture(ninja::kLogoUrl))
        ImGui::Image(tex, ImVec2(line_h, line_h));
    else
        ImGui::TextDisabled("ninja");

    ImGui::TableSetColumnIndex(1);
    draw_ninja_price(app, r, ambiguous);

    ImGui::TableSetColumnIndex(2);
    ImGui::BeginGroup();
    if (ambiguous) {
        // A trend across four different items is not a trend. The count is what the reader
        // needs here anyway: it says why there are two numbers in the column beside it.
        ImGui::TextDisabled("%zu variants", r.variants.size());
    } else {
        draw_spark(r.spark, kSparkW, line_h);
        if (r.spark.known) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(r.spark.change >= 0 ? kUp : kDown, "%+.0f%%",
                               static_cast<double>(r.spark.change));
        }
    }
    ImGui::EndGroup();

    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("poe.ninja \xe2\x80\x94 click to open the item's page");
        if (!r.label.empty()) ImGui::TextDisabled("Priced as: %s", r.label.c_str());
        if (!r.note.empty()) ImGui::TextColored(kWarn, "%s", r.note.c_str());
        if (r.listings > 0) ImGui::TextDisabled("%d listings behind the price", r.listings);
        if (r.fetched_at > 0)
            ImGui::TextDisabled("Updated %s ago",
                                trade::age_text(r.fetched_at, std::time(nullptr)).c_str());
        // Every variant with its own price: the one thing that turns "somewhere between these
        // two numbers" back into a price the reader can pick out for themselves.
        for (const ninja::Variant& v : r.variants) {
            ImGui::Separator();
            ImGui::Text("%s", v.label.empty() ? "(no variant)" : v.label.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", trade::price_text(v.price.amount).c_str(),
                                app.trade().currency_name(v.price.currency).c_str());
        }
        ImGui::EndTooltip();
    }
    ImGui::EndTable();
    return true;
}

/// Volumes on this market run to eight figures, and the reader wants the order of magnitude
/// rather than the digits: a market that moved seventeen million chaos in an hour is liquid and
/// one that moved sixty is a rumour.
std::string volume_text(double n) {
    char buf[32];
    if (n >= 1e6) std::snprintf(buf, sizeof buf, "%.1fM", n / 1e6);
    else if (n >= 1e4) std::snprintf(buf, sizeof buf, "%.0fk", n / 1e3);
    else std::snprintf(buf, sizeof buf, "%.0f", n);
    return buf;
}

/// What to call the item on its own side of a market. The exchange trades base types —
/// currency, cards, scarabs, fragments — so the base line is the whole name, and the resolved
/// one wherever the bundle gave us it.
std::string exchange_item_name(const item::Item& it) {
    if (!it.base_name.empty()) return it.base_name;
    if (!it.base_type.empty()) return it.base_type;
    return it.name;
}

/// One side of a market: what to call it and what to draw beside it. `currency` is set only
/// for the two denominators — those are named and pictured from their trade id like every
/// other price on screen — and everything else carries its own name and whatever symbol
/// `icon_for_item` could find for it.
struct MarketSide {
    const char* currency = nullptr;
    std::string name;
    std::string icon;
};

/// One side of a market as a value, `12 x [icon] Chaos Orb`. `times` is what tells a price
/// from a count of things: `19k Chaos Orb` traded, at `8.9 x Chaos Orb` each.
///
/// `named` is dropped on the summary line, which is the one place a market has to fit *both*
/// sides on a row: an item named there as well as pictured runs off the panel, and the glyph
/// is the thing players recognise an item by anyway. Never dropped when there is no glyph to
/// stand in for the name, and never for a currency, whose names are two words.
void draw_side(App& app, const std::string& amount, const MarketSide& side, bool times = true,
               bool named = true) {
    ImGui::TextUnformatted(times ? (amount + " x").c_str() : amount.c_str());
    if (side.currency) draw_currency(app, side.currency);
    else draw_symbol(app, side.icon, named || side.icon.empty() ? side.name : std::string());
}

/// One detail row of the market table: a label and a value in one of the two units.
void draw_exchange_row(App& app, const char* label, const std::string& amount,
                       const MarketSide& side, bool times = true) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (label) ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    draw_side(app, amount, side, times);
}

/// One market: what the hour cleared at, and the hour behind it.
///
/// The heading is the **volume-weighted average** — the two sides' traded volumes divided,
/// which is where in the band the market actually sat rather than the midpoint of two extremes
/// a single trade can set. It is stated against whichever of the two is worth more, so one
/// side is always a single unit: `4 x Winged Scarab = 1 x Chaos Orb` is how the trade is said
/// in game, and "0.25 chaos each" is not.
///
/// Underneath it, the hour it is an average of: volume on both sides and the two ends of the
/// band, in the same direction as the heading. **Stock is deliberately absent** — what is
/// standing in the book says how long a sale would take, not what the item is worth.
void draw_exchange_market(App& app, const MarketSide& item, const exchange::Rate& r,
                          const char* against) {
    if (!r.known()) return;
    const MarketSide other{against, {}, {}};
    const exchange::Reading v = exchange::read(r);
    // Which side is quoted as one unit, and therefore which unit every number below is in.
    const MarketSide& unit = v.inverted ? item : other;

    // No volume on one of the sides is no average, and then the band itself is the summary:
    // there is nothing else in the payload that says where the hour sat.
    std::string amount = trade::price_text(v.avg > 0 ? v.avg : v.low);
    if (v.avg <= 0 && v.high != v.low) amount += " \xe2\x80\x93 " + trade::price_text(v.high);

    ImGui::BeginGroup();
    draw_side(app, amount, unit, /*times=*/true, /*named=*/false);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextDisabled("=");
    ImGui::SameLine(0.0f, 6.0f);
    draw_side(app, "1", v.inverted ? other : item, /*times=*/true, /*named=*/false);
    ImGui::EndGroup();
    if (v.avg > 0 && ImGui::IsItemHovered())
        ImGui::SetTooltip("The hour's average, weighted by what actually traded");

    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    ImGui::Indent(8.0f);
    if (ImGui::BeginTable("market", 2, kFlags)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        // Both sides of the volume, because either one alone is half a sentence: a thousand
        // scarabs and four chaos is a different market from a thousand scarabs and four divine.
        if (r.volume > 0)
            draw_exchange_row(app, "Volume", volume_text(r.volume), item, false);
        if (r.volume_against > 0)
            draw_exchange_row(app, r.volume > 0 ? nullptr : "Volume",
                              volume_text(r.volume_against), other, false);
        draw_exchange_row(app, "Lowest", trade::price_text(v.low), unit);
        draw_exchange_row(app, "Highest", trade::price_text(v.high), unit);
        ImGui::EndTable();
    }
    ImGui::Unindent(8.0f);
}

/// What the **in-game currency exchange** did with this item in the last published hour.
///
/// Three states, and the middle one is the reason the bundle carries a flag at all:
///
/// - **A market this hour** — the table below, which is the price.
/// - **No market this hour, but the item trades there.** The feed is hourly and a thin item
///   (a Weeping Essence of Greed) goes hours without a trade, so silence is the normal case
///   rather than an answer. Drawing nothing here left the panel empty — poe.ninja has no price
///   for such an item either — and an empty panel reads as a check that failed. Saying so is
///   the answer: it tells the user where the item is sold and that nobody sold one recently,
///   which is itself worth knowing.
/// - **Not traded there, or a bundle that cannot say** — no section at all. Claiming either
///   way from a bundle with no exchange data would be a guess.
///
/// False when nothing was drawn, so the caller knows not to rule off an empty section.
bool draw_exchange_price(App& app, const item::Item& it) {
    const ExchangeService& x = app.currency_exchange();
    if (x.state() == ExchangeState::Loading) {
        ImGui::TextDisabled("Currency exchange\xe2\x80\xa6");
        return true;
    }
    const exchange::Listing* l = x.listing();
    if (!l) {
        if (!app.trades_on_exchange()) return false;
        ImGui::TextDisabled("Currency exchange");
        ImGui::Indent(8.0f);
        // Matched to the poe.ninja row's voice: what the source has to say, not what the app
        // failed to do. "No trades in the past hour" is a fact about the market; "no price"
        // would read as a fact about the check.
        //
        // But it is only a fact if the hour was actually read. A digest we failed to fetch says
        // nothing about what traded in it, and stating the market was quiet on the strength of
        // our own failed request is the one thing worse than saying nothing — the item still
        // gets no trade search either way, so the user would have no way to tell.
        ImGui::PushTextWrapPos(0.0f);
        if (x.state() == ExchangeState::Error)
            ImGui::TextColored(kWarn, "Traded here, but this hour's prices could not be fetched.");
        else
            ImGui::TextDisabled("Traded here, but no trades in the past hour.");
        ImGui::PopTextWrapPos();
        ImGui::Unindent(8.0f);
        return true;
    }

    ImGui::TextDisabled("Currency exchange");
    ImGui::Indent(8.0f);
    // The two orbs everything is denominated in are also the two commonest things to check, and
    // each is priced against the other — so those go through the same id-keyed path as any
    // price on screen, and everything else is found by name.
    MarketSide item;
    if (l->metadata_id == exchange::kChaosId) item.currency = "chaos";
    else if (l->metadata_id == exchange::kDivineId) item.currency = "divine";
    else {
        item.name = exchange_item_name(it);
        item.icon = icon_for_item(app, item.name);
    }
    draw_exchange_market(app, item, l->chaos, "chaos");
    draw_exchange_market(app, item, l->divine, "divine");
    // The hour is not a detail. An exchange price is always at least an hour old — the feed
    // publishes nothing about the hour in progress — and saying so is what keeps a stale
    // number from reading as a live one.
    //
    // In the **user's own clock and their own date format**: the digest is addressed by a UTC
    // hour, but a reader deciding whether a price is stale should have to do neither timezone
    // nor date-order arithmetic to read it. `%x` is whatever `LC_TIME` says a short date is
    // (`App::run` sets that from the environment, unlike `LC_NUMERIC`); the time is spelled
    // out rather than left to `%X`, which adds a seconds field that is always zero here. Drawn
    // in the system face — a locale's date can be Cyrillic or CJK, of which Fontin has none.
    if (const int64_t h = x.hour(); h > 0) {
        const std::time_t t = static_cast<std::time_t>(h + 3600);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        char when[64];
        if (std::strftime(when, sizeof when, "%x %H:%M", &local) == 0) when[0] = '\0';
        ImGui::PushFont(app.fonts().unicode, 0.0f);
        ImGui::TextDisabled("valid as of %s", when);
        ImGui::PopFont();
    }
    ImGui::Unindent(8.0f);
    return true;
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
        // Where the answer is, not just that there is no search: currency, cards, scarabs and
        // fragments have nothing a stat query could ask for — they are bought in bulk on the
        // in-game exchange — so the poe.ninja row above is the whole price check, and a bare
        // "Nothing to search" over it reads as a failure. An unidentified unique is the same
        // argument: the search is one question away, not missing.
        const item::Item* it = app.item();
        ImGui::TextDisabled("%s", it && it->needs_unique_choice() ? "Pick which unique it is"
                                  : app.plan().strategy == item::Strategy::Currency
                                      ? "Priced by poe.ninja, not by a trade search"
                                      : "Nothing to search");
    } else if (t.state() == TradeState::Ok) {
        // The total, not the number fetched: "20 of 4" would be a lie and "20 listings" hides
        // that there are two thousand more.
        ImGui::TextDisabled("%d match%s in %s", t.results().total,
                            t.results().total == 1 ? "" : "es", t.league().c_str());
    }
}

/// Why there is **no** search, for the items that get none at all. The filters, the buttons and
/// the listings are all gone for these, and an item sitting alone under two price rows with no
/// word about it reads as a check that gave up rather than as one that has already finished.
void draw_no_search_note(App& app) {
    ImGui::PushTextWrapPos(0.0f);
    if (app.trades_on_exchange()) {
        ImGui::TextDisabled("Traded on the in-game currency exchange, not through listings.");
    } else if (app.plan().strategy == item::Strategy::Currency) {
        ImGui::TextDisabled("Priced by poe.ninja, not by a trade search.");
    } else {
        for (const std::string& n : app.plan().notes)
            ImGui::TextColored(kWarn, "\xe2\x80\xa2 %s", n.c_str());
    }
    ImGui::PopTextWrapPos();
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
    draw_item_tooltip(it, app.derived(), app.fonts(), app.item_lexicon());
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
    draw_item_tooltip(*li.item, li.derived, app.fonts(), app.item_lexicon());
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

    // Who the user is, when they have told Settings. A listing of their own is otherwise
    // indistinguishable from the rest of the page, and the page is what a price is read off:
    // an own listing sitting at the top reads as the market's floor, which it is not.
    const std::string& me = app.config().account_name;

    for (size_t i = 0; i < t.results().listings.size(); ++i) {
        const trade::Listing& l = t.results().listings[i];
        ImGui::TableNextRow();
        const bool mine = !me.empty() && same_account(l.account, me);
        // After TableNextRow and before the row is drawn: the background is a channel of its
        // own, under everything the cells put down.
        if (mine) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, kOwnRow);
        ImGui::TableSetColumnIndex(0);
        // The whole handle, "#1234" included: the digits are what tells two players sharing a
        // name apart, and dropping them made the column look like a name it is not. Drawn in
        // the system face — PoE handles are routinely Cyrillic, Hangul or CJK, and Fontin has
        // not a glyph of any of it.
        //
        // A Selectable rather than text so the *row* is the hover target for the item tooltip
        // and lights up to say so. It spans every column, and AllowOverlap lets the price
        // cell's own hover (the fee) sit on top of it.
        //
        // The user's own row says so in words as well as in colour: the tint is what catches
        // the eye at a glance, and a green row is nothing at all to a reader who cannot see
        // green. Clipped away on a narrow panel, where the tint is still there.
        const std::string label =
            (mine ? l.account + "  (you)" : l.account) + "##row" + std::to_string(i);
        ImGui::PushFont(app.fonts().unicode, 0.0f);
        ImGui::Selectable(label.c_str(), false,
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
    // A search the trade site cannot be asked in the first place — currency, a card, anything
    // the in-game exchange trades — takes the whole filter half of the panel with
    // it: there is no query for the filters to shape and no listings for a note about an
    // unmatched modifier to have cost anybody. What is left is the item and its reference
    // prices, so the item moves back out of the gutter and into the column the filters had.
    const bool searchable = trade::searchable(app.plan()) && !app.trades_on_exchange();
    // The item itself lives in the gutter beside the panel, so the panel's own column is filters,
    // price and listings — the things that need the height. Drawn before the panel purely for
    // reading order; the two windows never overlap. 0 back means there was no gutter to draw it
    // in and the panel has to.
    const float gutter_top = it && searchable ? draw_item_card(app, *it) : 0.0f;
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

    // One line, and nothing that closes anything. The panel is open because the user asked
    // what an item is worth; an update notice here is allowed to be seen and then ignored,
    // which is what the dismiss is for.
    if (const update::Updater::Status ust = app.update_status();
        ust.has_news() && !app.update_dismissed()) {
        if (ust.state == update::Updater::State::Ready)
            ImGui::TextColored(kWarn, "v%s is ready — restart to update", ust.available.c_str());
        else
            ImGui::TextColored(kWarn, "v%s is out", ust.available.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Later")) app.dismiss_update_notice();
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
        // In the panel whenever it is not in the gutter: either the game window left no room
        // for one, or there are no filters for it to be making room for.
        if (gutter_top == 0.0f) {
            draw_item_tooltip(*it, app.derived(), app.fonts(), app.item_lexicon());
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Separator();
        }

        item::SearchPlan& plan = app.plan();
        if (!gd) {
            ImGui::TextDisabled("No pricing data yet, so nothing has been matched.");
        } else {
            // Every one of these is about a search: which one to run, what to ask it for, and
            // what had to be left out of the asking. None of it means anything for an item
            // nobody can search, and printing it there says a price check failed at something
            // it never attempted.
            if (searchable) {
                draw_strategy_picker(app, *it, plan);
                ImGui::Dummy(ImVec2(0, 4));
                // Above the filters, because until it is answered they are filters on nothing:
                // a unique is bought for its name, and the base is all this item has said.
                if (it->needs_unique_choice()) {
                    draw_unique_choice(app, *it);
                    ImGui::Dummy(ImVec2(0, 4));
                }
                draw_filters(*it, plan, app.fonts().has_comparison_glyphs);
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
            }
            // Each priced section is ruled off from the next, and only when it drew: two
            // separators with nothing between them is what an unconditional one gives on the
            // items that have one source of price and not the other.
            if (draw_reference_price(app)) ImGui::Separator();
            if (draw_exchange_price(app, *it)) ImGui::Separator();
            // An item that trades on the in-game exchange gets no Search and no Open in
            // browser: it has no listings for either to find, and a button that can only ever
            // come back empty reads as the item being unsellable rather than as the wrong
            // market having been asked.
            if (searchable) {
                draw_search_controls(app);
                draw_results(app, gutter_top);
            } else {
                draw_no_search_note(app);
            }
        }
    }
    ImGui::EndChild();

    if (footer_h > 0.0f) draw_debug_footer(app);

    ImGui::End();
}

} // namespace ppc
