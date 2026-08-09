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

/// Storage keys beside the grab: the domain, frozen at the press. A drag that pushes a knob
/// past an end widens the domain, and recomputing it every frame would rescale the track under
/// the knob being dragged — the number under the cursor would then race away from the cursor.
ImGuiID key_lo(ImGuiID id) { return id + 1; }
ImGuiID key_hi(ImGuiID id) { return id + 2; }

/// How far past an end a drag can pull, as a multiple of the known range. Not a claim about
/// what the affix can roll — nothing here knows that — only a stop far enough out that the
/// user gets to the number they meant, with the boxes for anything beyond it.
constexpr double kOvershoot = 2.0;

double round_to(double v, int dp) {
    const double p = std::pow(10.0, dp);
    return std::round(v * p) / p;
}

} // namespace

bool range_slider(const char* id, std::optional<double>& min, std::optional<double>& max,
                  double lo, double hi, int dp, float width) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    // What the modifier is known to roll, kept aside: once a bound has been pushed past it the
    // domain is wider than this, and the two ticks that say where the known range was are the
    // only thing left telling the reader which numbers are the affix's and which are theirs.
    const double known_lo = lo, known_hi = hi;

    // A track with no width is not a slider; a domain with no width would divide by zero
    // mapping a value onto it. Both are real — a modifier that can only roll one number — and
    // the honest answer to the second is a track the knobs sit in the middle of.
    if (min && max && *min > *max) std::swap(min, max);
    if (min) lo = std::min(lo, *min);
    if (max) hi = std::max(hi, *max);
    if (hi <= lo) hi = lo + 1.0;

    const float h = ImGui::GetFrameHeight();
    const float knob = h * kKnobFrac;
    const ImVec2 at = window->DC.CursorPos;
    const ImRect bb(at, ImVec2(at.x + width, at.y + h));
    const ImGuiID wid = window->GetID(id);

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, wid)) return false;

    // Frozen at the press, so a drag that widens the domain does not rescale the track it is
    // being dragged on.
    ImGuiStorage* store = window->DC.StateStorage;
    if (store->GetInt(wid, kNone) != kNone) {
        lo = static_cast<double>(store->GetFloat(key_lo(wid), static_cast<float>(lo)));
        hi = static_cast<double>(store->GetFloat(key_hi(wid), static_cast<float>(hi)));
    }

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
        return round_to(lo + static_cast<double>((x - x0) / span) * (hi - lo), dp);
    };

    // An absent bound parks at its own end of the track: the interval still reads as one, and
    // the knob is somewhere the user can pick it up.
    const float min_x = to_x(min.value_or(lo));
    const float max_x = to_x(max.value_or(hi));

    bool held = false, hovered = false;
    ImGui::ButtonBehavior(bb, wid, &hovered, &held);
    ImGuiStorage* state = window->DC.StateStorage;
    if (ImGui::IsItemActivated()) {
        // Nearer knob wins the press, and a tie goes to the max: the two coincide when the
        // interval has collapsed to a point, and widening it upwards is the commoner intent.
        state->SetInt(wid, std::abs(ImGui::GetIO().MousePos.x - min_x) <
                                   std::abs(ImGui::GetIO().MousePos.x - max_x)
                               ? kMin
                               : kMax);
        state->SetFloat(key_lo(wid), static_cast<float>(lo));
        state->SetFloat(key_hi(wid), static_cast<float>(hi));
    }
    if (!held) state->SetInt(wid, kNone);

    bool changed = false;
    if (held) {
        // **Not clamped to the domain**: the ends are what the affix is known to roll, not what
        // the user is allowed to ask for. `to_value` extrapolates outside the track, and the
        // stop is a couple of ranges out — far enough to reach the number, with the boxes for
        // anything past it.
        const double reach = (hi - lo) * kOvershoot;
        const double v = std::clamp(to_value(ImGui::GetIO().MousePos.x), lo - reach, hi + reach);
        if (state->GetInt(wid) == kMin) {
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

    ImDrawList* dl = window->DrawList;
    const float cy = (bb.Min.y + bb.Max.y) * 0.5f;
    const float t = h * kTrackFrac * 0.5f;
    const ImU32 track = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 accent = ImGui::GetColorU32(col::kAccent);
    const ImU32 edge = ImGui::GetColorU32(col::kBorder);

    dl->AddRectFilled(ImVec2(x0 - knob, cy - t), ImVec2(x1 + knob, cy + t), track, t);
    dl->AddRect(ImVec2(x0 - knob, cy - t), ImVec2(x1 + knob, cy + t), edge, t);
    // Where the **known** range sits, once a bound has been pushed outside it and the track is
    // wider than the affix is. Without these the widened track reads as the affix's own range,
    // which is a claim about other tiers that nothing here is entitled to make.
    if (known_lo > lo || known_hi < hi)
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
        dl->AddCircleFilled(ImVec2(x, cy), knob, set ? accent : ImGui::GetColorU32(ImGuiCol_FrameBg));
        dl->AddCircle(ImVec2(x, cy), knob, edge, 0, 1.5f);
    };
    draw_knob(min_x, min.has_value());
    draw_knob(max_x, max.has_value());
    return changed;
}

} // namespace ppc::ui
