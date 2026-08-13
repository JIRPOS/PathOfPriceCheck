#include "screens/mapcheck_screen.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "mapcheck/rate.hpp"
#include "screens/item_view.hpp"
#include "ui/glyphs.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"

namespace ppc {
namespace {

using mapcheck::Outlook;
using mapcheck::Verdict;

/// The tooltip's own sizes, as `item_view` sets them: a plate in small caps over the base
/// line, and the rest at the body size.
constexpr float kTitleSize = 18.0f;
constexpr float kPlateSize = 17.0f;
constexpr float kRowPad = 3.0f;      ///< above and below a modifier's lines, inside its tint
/// Between one row's tint and the next. Two modifiers the user answered the same way would
/// otherwise draw as a single block, and the list is meant to be counted at a glance.
constexpr float kRowGap = 2.0f;
constexpr float kGlyphColumn = 22.0f;

// The game's palette, as item_view uses it. Repeated rather than shared because that file's
// copies are its own private business and this panel is not a second caller of them.
constexpr ImU32 kColLabel = IM_COL32(127, 127, 127, 255);
constexpr ImU32 kColValue = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kColMod = IM_COL32(136, 136, 255, 255);
constexpr ImU32 kColAugmented = IM_COL32(136, 136, 255, 255);
/// Dulled towards the background from `kColMod`, and no further: an implicit shares the list
/// with the affixes now, and the grey a row that resolved to nothing is drawn in is only
/// (150,150,150). A tint between those two says "unreadable" to the reader far more often than
/// it says "implicit".
constexpr ImU32 kColImplicit = IM_COL32(140, 140, 220, 255);

ImU32 rarity_tint(item::Rarity r) {
    switch (r) {
    case item::Rarity::Magic: return IM_COL32(136, 136, 255, 255);
    case item::Rarity::Rare: return IM_COL32(255, 255, 119, 255);
    case item::Rarity::Unique: return IM_COL32(214, 129, 62, 255);
    default: return IM_COL32(200, 200, 200, 255);
    }
}

/// A dimmer rule than ImGui's own, matching the one the item card draws.
void draw_rule() {
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(90, 90, 90, 160));
    ImGui::Separator();
    ImGui::PopStyleColor();
}

void centred(const std::string& s, ImU32 colour) {
    const float w = ImGui::CalcTextSize(s.c_str()).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (w <= avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

Outlook outlook_of(const App& app) {
    return mapcheck::assess(mapcheck::tally(app.map_rows()));
}

ImVec4 outlook_colour(Outlook o) {
    switch (o) {
    case Outlook::Safe: return ImVec4(0.35f, 0.78f, 0.38f, 1.0f);
    case Outlook::Likely: return ImVec4(0.88f, 0.82f, 0.30f, 1.0f);
    case Outlook::Careful: return ImVec4(0.92f, 0.58f, 0.20f, 1.0f);
    case Outlook::Fatal: return ImVec4(0.90f, 0.28f, 0.24f, 1.0f);
    default: return ImVec4(0.62f, 0.64f, 0.70f, 1.0f); // NoMods and Unrated: no colour to give
    }
}

ui::Msg outlook_text(Outlook o, bool any_unrated) {
    switch (o) {
    case Outlook::NoMods: return ui::Msg::MapOutlookNoMods;
    case Outlook::Unrated: return ui::Msg::MapOutlookUnrated;
    case Outlook::Safe:
        return any_unrated ? ui::Msg::MapOutlookSafeUnrated : ui::Msg::MapOutlookSafe;
    case Outlook::Likely: return ui::Msg::MapOutlookLikely;
    case Outlook::Careful: return ui::Msg::MapOutlookCareful;
    case Outlook::Fatal: return ui::Msg::MapOutlookFatal;
    }
    return ui::Msg::MapOutlookNoMods;
}

/// The glyph the banner leads with: the worst verdict on the map, except that a map nobody has
/// read yet is a question rather than a tick.
const char* outlook_glyph(Outlook o) {
    switch (o) {
    case Outlook::Safe: return ui::verdict_glyph(Verdict::Safe);
    case Outlook::Likely:
    case Outlook::Careful: return ui::verdict_glyph(Verdict::Dangerous);
    case Outlook::Fatal: return ui::verdict_glyph(Verdict::Deadly);
    default: return ui::verdict_glyph(Verdict::Unrated);
    }
}

/// The one line the popup exists to be read at a glance for. Drawn on its own tinted strip
/// across the whole width, above the item, because it is the answer and the item is the
/// working.
void draw_outlook(App& app) {
    const Outlook o = outlook_of(app);
    const mapcheck::Tally t = mapcheck::tally(app.map_rows());
    const ImVec4 tint = outlook_colour(o);

    const float pad = ImGui::GetStyle().FramePadding.y;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    // A wash rather than the colour itself: the sentence over it is what has to be read, and
    // white text on a solid green bar is not a tooltip, it is a notification.
    const ImU32 wash = ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.16f));
    const std::string text = ui::text(outlook_text(o, t.unrated > 0));

    ImGui::PushStyleColor(ImGuiCol_Text, tint);
    const float glyph_w = app.fonts().has_glyphs ? kGlyphColumn : 0.0f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - glyph_w);
    const float text_h = ImGui::CalcTextSize(text.c_str(), nullptr, false, w - glyph_w).y;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p0.x - 2, p0.y - pad),
                                              ImVec2(p0.x + w + 2, p0.y + text_h + pad), wash,
                                              3.0f);
    if (app.fonts().has_glyphs) {
        ImGui::TextUnformatted(outlook_glyph(o));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kGlyphColumn -
                             ImGui::CalcTextSize(outlook_glyph(o)).x);
    }
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, pad));
}

/// The name plate, as the item card draws it but without the room for a gutter.
void draw_plate(App& app, const item::Item& it) {
    const ImU32 colour = rarity_tint(it.rarity);
    ImGui::PushFont(app.fonts().small_caps, kPlateSize);
    if (!it.name.empty()) centred(it.name, colour);
    std::string plate = it.base_type;
    if (it.map_tier) plate += " (Tier " + std::to_string(*it.map_tier) + ")";
    centred(plate, colour);
    ImGui::PopFont();
}

/// A lexicon term as a label: the game writes "Requires " with the space that joins it to what
/// follows, and a label is followed by its own colon instead.
std::string as_label(std::string_view term) {
    while (!term.empty() && term.back() == ' ') term.remove_suffix(1);
    return std::string(term);
}

/// The job levels a heist item demands, as one pair: `Requires: Deception 5, Engineering 4`.
///
/// The game writes one sentence per job and a fully revealed blueprint asks for several — six on
/// the Tunnels capture — so drawn as the game writes them they are the longest thing on the panel
/// and most of it is the word "Requires". Folded here into the label the rest of the block has,
/// keeping whatever the client annotated the level with — "(unmet)" is the reason to read the line
/// at all. Both wordings come out of the lexicon, so a translated client folds the same way.
std::string heist_jobs_value(const item::Item& it, const data::Lexicon& lex) {
    const std::string_view prefix = lex.term(data::Term::HeistJobPrefix);
    const std::string_view level = lex.term(data::Term::HeistJobLevel);
    std::string out;
    for (const item::Property& p : it.properties) {
        if (p.key != data::PropertyKey::HeistJob) continue;
        std::string_view line = p.value;
        if (line.starts_with(prefix)) line.remove_prefix(prefix.size());
        if (!out.empty()) out += ", ";
        // "Deception (Level 5 (unmet))" -> "Deception 5 (unmet)". A line the terms do not fit
        // is kept whole rather than cut at a guess: it is still the job and the level.
        const size_t at = line.find(level);
        if (at == std::string_view::npos || line.back() != ')') {
            out += line;
            continue;
        }
        out += line.substr(0, at);
        out += ' ';
        out += line.substr(at + level.size(), line.size() - at - level.size() - 1);
    }
    return out;
}

/// The numbers a map is opened for, laid out across the panel instead of one to a line.
///
/// The game prints "Item Quantity: +107%" on three lines of its own; here they are a run that
/// wraps, which is the whole of what "compact" means for this block. The labels are the ones
/// the client printed rather than shortened ones — a shorter word would have to be invented per
/// language, and the wrap already buys the space.
void draw_properties(const item::Item& it, const data::Lexicon& lex) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const std::string jobs = heist_jobs_value(it, lex);
    float x = 0.0f;
    bool first = true;
    bool jobs_drawn = false;
    for (const item::Property& p : it.properties) {
        std::string value = p.value;
        std::string label = p.label;
        if (p.key == data::PropertyKey::HeistJob) {
            // All of them at once, where the first one was printed, so the block still reads in
            // the order the game wrote it.
            if (jobs_drawn || jobs.empty()) continue;
            jobs_drawn = true;
            label = as_label(lex.term(data::Term::HeistJobPrefix));
            value = jobs;
        }
        if (label.empty()) continue; // prose the game prints among the properties
        label += ": ";
        const float w =
            ImGui::CalcTextSize(label.c_str()).x + ImGui::CalcTextSize(value.c_str()).x;
        // Measured against what is left on the line rather than against the pair's own width:
        // a pair that does not fit starts a new line, and one that has never fitted anywhere
        // still gets one to itself and is wrapped by ImGui.
        if (!first && x + gap + w <= avail) {
            ImGui::SameLine(0.0f, gap);
            x += gap + w;
        } else {
            x = w;
        }
        first = false;
        ImGui::PushStyleColor(ImGuiCol_Text, kColLabel);
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, p.augmented ? kColAugmented : kColValue);
        // Wrapped, not clipped. Every pair the block was first written for was a number and
        // fitted; the folded job list on a fully revealed blueprint is longer than the panel is
        // wide, and without a wrap position ImGui cuts it off at the edge instead. `x` is left
        // holding the unwrapped width on purpose — it is then wider than the line can be, so the
        // next pair starts a line of its own rather than being placed against a stale cursor.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

/// Which table the verdicts come from. In the popup as well as in Settings because the answer
/// is per character and the popup is where the character is: switching in the middle of a map
/// is the case this exists for.
void draw_profile_row(App& app) {
    const mapcheck::Store& store = app.map_store();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kLabel);
    ImGui::TextUnformatted(ui::text(ui::Msg::MapProfile));
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const std::string preview =
        store.current().empty() ? ui::text(ui::Msg::MapProfileNone) : store.current();
    if (ImGui::BeginCombo("##profile", preview.c_str())) {
        for (const std::string& name : store.names()) {
            const bool sel = name == store.current();
            if (ImGui::Selectable(name.c_str(), sel)) app.select_map_profile(name);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        if (store.names().empty()) ImGui::TextDisabled("%s", ui::text(ui::Msg::MapNoProfile));
        ImGui::EndCombo();
    }
}

/// One modifier: its verdict on the left, its printed lines beside it, the whole strip a
/// button.
///
/// **Nothing is split and nothing is merged.** A hybrid modifier keeps both its lines in one
/// row, and two modifiers wording the same thing stay two rows — the item printed them that way
/// and this panel is a reading of the item.
void draw_row(App& app, size_t index) {
    const mapcheck::Row& row = app.map_rows()[index];
    ImGui::PushID(static_cast<int>(index));

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float text_x = ImGui::GetCursorPosX() + kGlyphColumn;

    // **The words are drawn first and the tint is painted behind them afterwards.** A row's
    // height is not knowable before the draw: a line may wrap, and ImGui puts `ItemSpacing.y`
    // between each of an affix's lines because each is an item of its own. Measuring the same
    // text a second time to guess at that is what made a three-line affix come out two
    // spacings short, so its tint stopped where the next row's began and the colours bled into
    // each other's last line. A split channel lets the rectangle be filled in once the text
    // has said how tall it is, and there is then only one answer rather than two that have to
    // agree.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + kRowPad));
    ImGui::BeginGroup();
    if (row.rateable()) {
        // Dim for unrated: the column stays the same width on every row, so the list reads as
        // answers with the blanks visible rather than as text starting in three places.
        const ImVec4 glyph_col =
            row.verdict == Verdict::Unrated ? ui::col::kTextDim : ui::verdict_colour(row.verdict);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(glyph_col.x, glyph_col.y, glyph_col.z, 1.0f));
        ImGui::TextUnformatted(app.fonts().has_glyphs ? ui::verdict_glyph(row.verdict)
                                                      : ui::verdict_word(row.verdict));
        ImGui::PopStyleColor();
        // On the glyph's own line, not under it. Without this the modifier starts one line
        // below its verdict and the row is a line taller than it needs to be.
        ImGui::SameLine(0.0f, 0.0f);
    }

    ImGui::SetCursorPosX(text_x);
    ImGui::PushTextWrapPos(0.0f);
    // Implicits are rateable now and still say which they are, the way the game colours them:
    // what the base carries and what it rolled are different things to be deciding about.
    const bool implicit = row.mod() && row.mod()->type == data::ModType::Implicit;
    ImGui::PushStyleColor(ImGuiCol_Text,
                          !row.rateable() ? ImU32(IM_COL32(150, 150, 150, 255))
                          : implicit      ? kColImplicit
                                          : kColMod);
    // Every line of every modifier the affix printed: one affix is one row and one decision,
    // which is what the verdict is keyed on.
    for (const item::Modifier* m : row.mods)
        for (const std::string& l : m->lines) {
            ImGui::SetCursorPosX(text_x);
            ImGui::TextUnformatted(strip_roll_ranges(l).c_str());
        }
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    // What the row actually came out as, wrapping and inter-line spacing included.
    const float h = ImGui::GetItemRectSize().y + kRowPad * 2.0f;

    dl->ChannelsSetCurrent(0);
    const ImVec4 tint = ui::verdict_colour(row.verdict);
    if (tint.w > 0.0f)
        dl->AddRectFilled(ImVec2(p0.x - 2, p0.y), ImVec2(p0.x + w + 2, p0.y + h),
                          ImGui::GetColorU32(tint), 3.0f);
    dl->ChannelsMerge();

    // The whole strip is the target, not the words in it: what is being aimed at is the
    // modifier. Placed over the text, which is not interactive, so nothing competes for it.
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##rate", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && row.rateable()) {
        dl->AddRect(ImVec2(p0.x - 2, p0.y), ImVec2(p0.x + w + 2, p0.y + h),
                    ImGui::GetColorU32(ui::col::kBorder), 3.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    // Left walks the four states; right puts it straight back to unrated, which is otherwise
    // three clicks away from deadly and is the one a misclick needs.
    if (row.rateable()) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) app.rate_map_row(index);
        else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            app.rate_map_row(index, Verdict::Unrated);
    }

    // A wording the data cannot identify has nothing to key a verdict on, and saying so on
    // hover is better than a row that quietly does nothing when it is clicked.
    if (hovered && !row.rateable()) ImGui::SetTooltip("%s", ui::text(ui::Msg::MapUnresolved));

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + kRowGap));
    ImGui::PopID();
}

/// The name of the screen and the disc that leaves it, in the frame's top edge.
void draw_header(App& app) {
    const float h = ImGui::GetFrameHeight();
    ImGui::PushFont(app.fonts().small_caps, kTitleSize);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kTitle);
    ImGui::TextUnformatted(ui::text(ui::Msg::MapCheckTitle));
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - h);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::col::kClose);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::col::kCloseHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::col::kCloseHovered);
    if (ImGui::Button("X", ImVec2(h, h))) app.close_overlay();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ui::text(ui::Msg::Close));
}

} // namespace

namespace ui {

const char* verdict_glyph(Verdict v) {
    switch (v) {
    case Verdict::Safe: return kGlyphSafe;
    case Verdict::Dangerous: return kGlyphDangerous;
    case Verdict::Deadly: return kGlyphDeadly;
    default: return kGlyphUnrated;
    }
}

const char* verdict_word(Verdict v) {
    switch (v) {
    case Verdict::Safe: return "+";
    case Verdict::Dangerous: return "!";
    case Verdict::Deadly: return "X";
    default: return "?";
    }
}

ImVec4 verdict_colour(Verdict v) {
    switch (v) {
    case Verdict::Safe: return ImVec4(0.25f, 0.62f, 0.30f, 0.30f);
    case Verdict::Dangerous: return ImVec4(0.78f, 0.66f, 0.18f, 0.30f);
    case Verdict::Deadly: return ImVec4(0.72f, 0.18f, 0.16f, 0.32f);
    default: return ImVec4(0, 0, 0, 0); // unrated is the panel's own background
    }
}

} // namespace ui

void draw_mapcheck_screen(App& app) {
    const ui::Theme theme(app.config().reduce_transparency);
    const item::Item* it = app.item();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    // The whole SDL window, which App has sized generously; what the content actually came to
    // is measured below and the window follows on the next frame.
    //
    // Deliberately **not** ImGui's own auto-fit. A window's size is what it was laid out at,
    // so on the first frame `GetWindowSize` is the size it was given rather than the size its
    // content needs — feeding that back shrank the window to a title bar's height and, since
    // ImGui clamps a window to the viewport, it could never grow out of it again.
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("MapCheck", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    draw_header(app);
    if (!it) { // never in practice: the screen is only opened once there is an item
        ImGui::End();
        return;
    }

    draw_rule();
    draw_outlook(app);
    draw_rule();
    ImGui::PushFont(app.fonts().small_caps, 0.0f);
    draw_plate(app, *it);
    if (!it->properties.empty() || it->item_level) {
        draw_rule();
        draw_properties(*it, app.item_lexicon());
    }
    ImGui::PopFont();

    draw_rule();
    draw_profile_row(app);

    if (app.map_store().current().empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", ui::text(ui::Msg::MapNoProfileHelp));
        ImGui::PopTextWrapPos();
    }
    if (!app.map_rows().empty()) {
        draw_rule();
        ImGui::PushFont(app.fonts().small_caps, 0.0f);
        for (size_t i = 0; i < app.map_rows().size(); ++i) draw_row(app, i);
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", ui::text(ui::Msg::MapRateHint));
        ImGui::PopTextWrapPos();
    }

    // Where the content ended, plus the padding under it — the height the window needs, read
    // off the cursor rather than off the window for the reason `Begin` gives above.
    app.set_mapcheck_height(ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
    ImGui::End();
}

} // namespace ppc
