#include "data/stat_matcher.hpp"

#include <algorithm>
#include <cmath>

#include "data/stat_normalize.hpp"

namespace ppc::data {
namespace {

/// How many clipboard lines one modifier may span. Hybrids are two; the cap only bounds how
/// far a failed match will keep reaching forward.
constexpr size_t kMaxModLines = 4;

/// The game appends this to a roll that item level cannot scale.
constexpr std::string_view kUnscalableSuffix = " (unscalable value)";

double pow10i(int n) {
    double p = 1.0;
    for (int i = 0; i < n; ++i) p *= 10.0;
    return p;
}

} // namespace

bool is_reminder_text(std::string_view line) {
    // Trim, then require the whole line to be one parenthesised block.
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
        line.remove_suffix(1);
    return line.size() >= 2 && line.front() == '(' && line.back() == ')';
}

double incr_roll(double v, double percent, int dp) {
    const double scaled = v + v * percent / 100.0;
    const double p = pow10i(dp);
    return std::trunc(scaled * p) / p;
}

std::optional<StatMatch> match_stat(const GameData& gd, std::span<const std::string> lines,
                                    size_t start, const MatchContext& ctx) {
    if (start >= lines.size()) return std::nullopt;

    std::string join;
    bool unscalable = false;

    for (size_t end = start; end < lines.size() && end - start < kMaxModLines; ++end) {
        std::string_view line = lines[end];
        if (is_reminder_text(line)) continue;
        if (line.size() > kUnscalableSuffix.size() && line.ends_with(kUnscalableSuffix)) {
            line.remove_suffix(kUnscalableSuffix.size());
            unscalable = true;
        }
        if (!join.empty()) join.push_back('\n');
        join.append(line);

        for (const std::string& cand : candidates(join)) {
            const Stat* s = gd.find_stat(cand, ctx.mod_type);
            if (!s) continue;
            const StatMatcher* m = s->matcher_for(cand);
            if (!m) continue;

            StatMatch out;
            out.stat = s;
            out.matcher = m;
            out.mod_type = ctx.mod_type;
            out.unscalable = unscalable;
            out.lines_consumed = end - start + 1;

            // Rolls come from the tokens the winning candidate left as '#'. Re-scanning the
            // join is cheaper than threading the token list out of the candidate loop, and
            // keeps the normalizer's contract the only thing that has to stay in step.
            const std::string text = strip_empty_parens(join);
            const std::vector<NumberToken> toks = scan_numbers(text);
            for (const NumberToken& t : toks) out.rolls.push_back(t.value);

            bool have_bounds = false;
            double lo = 0, hi = 0;
            for (const NumberToken& t : toks) {
                if (!t.numeric_bounds) continue;
                lo = have_bounds ? std::min(lo, t.bound_min) : t.bound_min;
                hi = have_bounds ? std::max(hi, t.bound_max) : t.bound_max;
                have_bounds = true;
            }
            // Per roll as well: which of a mod's numbers a filter is built from is the
            // caller's business, and the merged pair above cannot answer it.
            if (have_bounds)
                for (const NumberToken& t : toks)
                    out.roll_bounds.emplace_back(t.numeric_bounds ? t.bound_min : t.value,
                                                 t.numeric_bounds ? t.bound_max : t.value);

            // A wording with no number still stands for a roll.
            if (out.rolls.empty() && m->value) out.rolls.push_back(*m->value);

            // matcher.negate says the *wording* is the inverse: store the roll in the
            // stat's canonical direction so summing and comparing work. This is not
            // trade.inverted, which says the trade site indexes the stat with the opposite
            // sign and is applied when the query is built, not here.
            if (m->negate) {
                out.negated = true;
                for (double& r : out.rolls) r = -r;
                for (auto& [blo, bhi] : out.roll_bounds) {
                    const double a = -bhi, b = -blo;
                    blo = a;
                    bhi = b;
                }
                if (have_bounds) {
                    const double a = -hi, b = -lo;
                    lo = a;
                    hi = b;
                }
            }

            // A catalyst scales the mods it applies to, and the clipboard prints the *unscaled*
            // roll and range while the tooltip — and trade — carry the scaled one.
            if (ctx.roll_incr != 0 && !out.unscalable) {
                for (double& r : out.rolls) r = incr_roll(r, ctx.roll_incr, s->dp);
                for (auto& [blo, bhi] : out.roll_bounds) {
                    blo = incr_roll(blo, ctx.roll_incr, s->dp);
                    bhi = incr_roll(bhi, ctx.roll_incr, s->dp);
                }
                if (have_bounds) {
                    lo = incr_roll(lo, ctx.roll_incr, s->dp);
                    hi = incr_roll(hi, ctx.roll_incr, s->dp);
                }
            }

            if (!out.rolls.empty()) {
                // A two-number mod ("Adds 5 to 12") is filtered on its average.
                double sum = 0;
                for (double r : out.rolls) sum += r;
                out.value = sum / static_cast<double>(out.rolls.size());
            }

            if (have_bounds) {
                // Legacy mods exist whose positive and negative wordings were swapped.
                if (lo > hi) std::swap(lo, hi);
                if (out.value < lo) {
                    lo = out.value;
                    out.legacy = true; // predates the current ranges; needs a Divine to fix
                }
                if (out.value > hi) {
                    hi = out.value;
                    out.legacy = true;
                }
                out.min = lo;
                out.max = hi;
            }
            return out;
        }
    }
    return std::nullopt;
}

} // namespace ppc::data
