#include "ui/range_slider.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/theme.hpp"

namespace ppc::ui {
namespace {

/// How thick the track is, and how big a knob sits on it, as fractions of the widget's height.
/// The knob has to be a comfortable mouse target at a 13px UI scale, which is what puts it at
/// roughly a third of the frame height rather than at the track's own thickness.
constexpr float kTrackFrac = 0.28f;
constexpr float kKnobFrac = 0.34f;

/// Which knob a drag is carrying, stored per widget between frames: the choice is made once, on
/// the press, and must not be remade mid-drag — dragging the min knob past the max would
/// otherwise hand the drag to the other one halfway across the track.
enum Grab : int { kNone = 0, kMin = 1, kMax = 2 };

/// Storage keys beside the grab: the live domain and whether one has been stored. It is frozen at
/// the press, because a drag that pushes a knob past an end widens it and recomputing every frame
/// would rescale the track under the knob being dragged — the number under the cursor would then
/// race away from the cursor. It also outlives the drag, since a track grown by pegging has to
/// still be grown on the next frame.
ImGuiID key_lo(ImGuiID id) { return id + 1; }
ImGuiID key_hi(ImGuiID id) { return id + 2; }
ImGuiID key_have(ImGuiID id) { return id + 3; }

/// How far past an end a drag can pull, as a multiple of the known range. Not a claim about
/// what the affix can roll — nothing here knows that — only a stop far enough out that the
/// user gets to the number they meant, with the boxes for anything beyond it.
constexpr double kOvershoot = 2.0;

/// How much a knob released hard against an end adds to the track, as a fraction of the range it
/// started with, and the least it can add. The floor is what makes the gesture work on the small
/// numbers it matters most on: a fifth of an attack speed's `1.30 to 1.45` is 0.03, which is not
/// room for anything.
constexpr double kPegGrowth = 0.2;
constexpr double kPegFloor = 1.0;

double round_to(double v, int dp) {
    const double p = std::pow(10.0, dp);
    return std::round(v * p) / p;
}

} // namespace

bool range_slider(const char* id, std::optional<double>& min, std::optional<double>& max,
                  const RangeTrack& track) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    // What the modifier is known to roll, kept aside: once a bound has been pushed past it the
    // domain is wider than this, and the two ticks that say where the known range was are the
    // only thing left telling the reader which numbers are the affix's and which are theirs.
    const double known_lo = track.lo, known_hi = track.hi;

    const float h = ImGui::GetFrameHeight();
    const float knob = h * kKnobFrac;
    const ImVec2 at = window->DC.CursorPos;
    const ImRect bb(at, ImVec2(at.x + track.width, at.y + h));
    const ImGuiID wid = window->GetID(id);

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, wid)) return false;

    ImGuiStorage* store = window->DC.StateStorage;
    if (track.reset) { // a different row, behind the same id
        store->SetInt(key_have(wid), 0);
        store->SetInt(wid, kNone);
    }
    double lo = track.lo, hi = track.hi;
    if (store->GetInt(key_have(wid), 0) != 0) {
        lo = static_cast<double>(store->GetFloat(key_lo(wid), static_cast<float>(lo)));
        hi = static_cast<double>(store->GetFloat(key_hi(wid), static_cast<float>(hi)));
    }

    // A track with no width is not a slider; a domain with no width would divide by zero
    // mapping a value onto it. Both are real — a modifier that can only roll one number — and
    // the honest answer to the second is a track the knobs sit in the middle of.
    //
    // **A crossed interval is drawn, not corrected.** This widget is drawn every frame, including
    // the frames in the middle of a number being typed into the boxes beside it, and `200` typed
    // over a floor of `192` is `2` for two keystrokes. Reordering the caller's bounds as a side
    // effect of drawing would make those two keystrokes permanent. So the domain is widened by
    // both bounds whichever way round they are, and the knobs simply cross; the drag below keeps
    // them in order because there a crossing *is* the gesture, and the editor puts a typed one
    // back in order when the box is left.
    if (store->GetInt(wid, kNone) == kNone) { // not mid-drag, where the frozen domain is the point
        for (const std::optional<double>& v : {min, max})
            if (v) {
                lo = std::min(lo, *v);
                hi = std::max(hi, *v);
            }
    }
    lo = std::max(lo, -kRangeLimit);
    hi = std::min(hi, kRangeLimit);
    if (hi <= lo) hi = lo + 1.0;

    // The knobs are inset by their own radius, so the one at each end of the domain is drawn
    // fully on the widget rather than half off it.
    const float x0 = bb.Min.x + knob;
    const float x1 = bb.Max.x - knob;
    const float span = std::max(1.0f, x1 - x0);
    const auto to_x = [&](double v) {
        const auto t = static_cast<float>((v - lo) / (hi - lo));
        return x0 + std::clamp(t, 0.0f, 1.0f) * span;
    };
    const auto to_value = [&](float x) {
        return round_to(lo + static_cast<double>((x - x0) / span) * (hi - lo), track.dp);
    };

    // An absent bound parks at its own end of the track: the interval still reads as one, and
    // the knob is somewhere the user can pick it up.
    const float min_x = to_x(min.value_or(lo));
    const float max_x = to_x(max.value_or(hi));

    bool held = false, hovered = false;
    ImGui::ButtonBehavior(bb, wid, &hovered, &held);
    if (ImGui::IsItemActivated()) {
        // Nearer knob wins the press, and a tie goes to the max: the two coincide when the
        // interval has collapsed to a point, and widening it upwards is the commoner intent.
        store->SetInt(wid, std::abs(ImGui::GetIO().MousePos.x - min_x) <
                                   std::abs(ImGui::GetIO().MousePos.x - max_x)
                               ? kMin
                               : kMax);
        store->SetFloat(key_lo(wid), static_cast<float>(lo));
        store->SetFloat(key_hi(wid), static_cast<float>(hi));
        store->SetInt(key_have(wid), 1);
    }

    bool changed = false;
    if (held) {
        // **Not clamped to the domain**: the ends are what the affix is known to roll, not what
        // the user is allowed to ask for. `to_value` extrapolates outside the track, and the
        // stop is a couple of ranges out — far enough to reach the number, with the boxes for
        // anything past it.
        const double reach = (hi - lo) * kOvershoot;
        const double v = std::clamp(to_value(ImGui::GetIO().MousePos.x),
                                    std::max(lo - reach, -kRangeLimit),
                                    std::min(hi + reach, kRangeLimit));
        if (store->GetInt(wid) == kMin) {
            if (min != v) changed = true;
            min = v;
            // Carried rather than clamped: an interval collapsed to a point is a legitimate
            // search, and having to aim at the other knob to make one is not.
            if (max && *max < v) max = v;
        } else {
            if (max != v) changed = true;
            max = v;
            if (min && *min > v) min = v;
        }
    }
    if (ImGui::IsItemDeactivated()) {
        store->SetInt(wid, kNone);
        // Released hard against an end: the user was asking for further out than the track goes,
        // and handing back a track exactly as long is what makes the widget feel stuck. Grow it,
        // and only on the end that was pegged — the other one has not been asked about.
        const double grow = std::max((known_hi - known_lo) * kPegGrowth, kPegFloor);
        if (min && *min <= lo) lo = std::max(lo - grow, -kRangeLimit);
        if (max && *max >= hi) hi = std::min(hi + grow, kRangeLimit);
        store->SetFloat(key_lo(wid), static_cast<float>(lo));
        store->SetFloat(key_hi(wid), static_cast<float>(hi));
        store->SetInt(key_have(wid), 1);
    }

    ImDrawList* dl = window->DrawList;
    const float cy = (bb.Min.y + bb.Max.y) * 0.5f;
    const float t = h * kTrackFrac * 0.5f;
    const ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 accent = ImGui::GetColorU32(col::kAccent);
    const ImU32 edge = ImGui::GetColorU32(col::kBorder);

    dl->AddRectFilled(ImVec2(x0 - knob, cy - t), ImVec2(x1 + knob, cy + t), bg, t);
    dl->AddRect(ImVec2(x0 - knob, cy - t), ImVec2(x1 + knob, cy + t), edge, t);
    // Where the **known** range sits, once the track is wider than the affix is. Without these
    // the widened track reads as the affix's own range, which is a claim about other tiers that
    // nothing here is entitled to make. A derived track has no such range to mark, and ticking
    // one on would be inventing the claim outright.
    if (track.published && (known_lo > lo || known_hi < hi))
        for (const double v : {known_lo, known_hi}) {
            const float x = to_x(v);
            dl->AddLine(ImVec2(x, cy - t * 2.2f), ImVec2(x, cy + t * 2.2f), edge, 1.0f);
        }
    // What the search would accept, as the lit part of the track. This is the reading the whole
    // widget exists for: how much of what the modifier can roll is being asked for.
    dl->AddRectFilled(ImVec2(std::min(min_x, max_x), cy - t), ImVec2(std::max(min_x, max_x), cy + t),
                      accent, t);

    // Hollow for an absent bound, filled for one the search will send. The difference is the
    // difference between "no ceiling" and "a ceiling that happens to be the top of the range",
    // which are different searches and look identical without it.
    const auto draw_knob = [&](float x, bool set) {
        dl->AddCircleFilled(ImVec2(x, cy), knob, set ? accent : bg);
        dl->AddCircle(ImVec2(x, cy), knob, edge, 0, 1.5f);
    };
    draw_knob(min_x, min.has_value());
    draw_knob(max_x, max.has_value());
    return changed;
}

} // namespace ppc::ui
