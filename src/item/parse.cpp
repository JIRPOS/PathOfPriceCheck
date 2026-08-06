#include "item/item.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace ppc::item {
namespace {

constexpr std::string_view kSeparator = "--------";

/// Lines that are a flag on their own. Influence lines are handled separately.
constexpr std::array<std::string_view, 8> kFlagLines{
    "Corrupted", "Unidentified", "Mirrored", "Split",
    "Synthesised Item", "Fractured Item", "Veiled", "Unmodifiable"};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

/// The number `s` starts with, or nothing. Deliberately `from_chars`: the game always writes
/// '.', and `strtod` reads that as an integer under any locale whose separator is ',' — which
/// turned every "Attacks per Second: 1.79" into 1.
std::optional<double> parse_number(std::string_view s) {
    s = trim(s);
    const bool negative = !s.empty() && s.front() == '-';
    // from_chars rejects a leading '+', and item text is full of them.
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) s.remove_prefix(1);
    double v = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return std::nullopt;
    return negative ? -v : v;
}

/// The first number anywhere in `s`, sign included.
std::optional<double> first_number(std::string_view s) {
    for (size_t i = 0; i < s.size(); ++i) {
        const bool digit = std::isdigit(static_cast<unsigned char>(s[i])) != 0;
        const bool signed_digit = (s[i] == '+' || s[i] == '-') && i + 1 < s.size() &&
                                  std::isdigit(static_cast<unsigned char>(s[i + 1]));
        if (digit || signed_digit) return parse_number(s.substr(i));
    }
    return std::nullopt;
}

std::optional<int> first_int(std::string_view s) {
    if (const std::optional<double> d = first_number(s)) return static_cast<int>(*d);
    return std::nullopt;
}

/// Drop the parentheticals the game appends to a *value* — "(augmented)", "(gem)", "(unmet)",
/// "(Max)" — reporting whether an augment was among them. A range such as an Advanced Mod
/// Descriptions "(229-263)" is not one of them and survives.
std::string strip_value_annotations(std::string_view s, bool* augmented) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '(') {
            const size_t close = s.find(')', i);
            if (close != std::string_view::npos) {
                const std::string_view inner = s.substr(i + 1, close - i - 1);
                if (inner == "augmented" || inner == "gem" || inner == "unmet" ||
                    inner == "Max" || inner == "fractured") {
                    if (inner == "augmented" && augmented) *augmented = true;
                    i = close + 1;
                    while (!out.empty() && out.back() == ' ') out.pop_back();
                    if (i < s.size() && s[i] == ' ') ++i;
                    if (!out.empty() && i < s.size()) out.push_back(' ');
                    continue;
                }
            }
        }
        out.push_back(s[i]);
        ++i;
    }
    return std::string(trim(out));
}

/// "25-98", a single "42", or the Advanced Mod Descriptions form "34(33-48)" — the roll is
/// taken and the bounds dropped, because a property's bounds are the sum of its mods' and are
/// re-derived from the mods themselves.
std::optional<DamageRange> parse_damage(std::string_view s) {
    s = trim(s);
    if (const size_t paren = s.find('('); paren != std::string_view::npos)
        s = trim(s.substr(0, paren));
    if (s.empty()) return std::nullopt;
    const size_t dash = s.find('-', s.front() == '+' || s.front() == '-' ? 1 : 0);
    DamageRange r;
    if (dash == std::string_view::npos) {
        const std::optional<double> v = parse_number(s);
        if (!v) return std::nullopt;
        r.min = r.max = *v;
        return r;
    }
    const std::optional<double> lo = parse_number(s.substr(0, dash));
    const std::optional<double> hi = parse_number(s.substr(dash + 1));
    if (!lo || !hi) return std::nullopt;
    r.min = *lo;
    r.max = *hi;
    return r;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t nl = text.find('\n', pos);
        out.emplace_back(trim(
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos)));
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    return out;
}

using Section = std::vector<std::string>;

std::vector<Section> split_sections(std::string_view text) {
    std::vector<Section> out;
    Section current;
    for (std::string& line : split_lines(text)) {
        if (line == kSeparator) {
            out.push_back(std::move(current));
            current.clear();
            continue;
        }
        if (line.empty()) continue;
        current.push_back(std::move(line));
    }
    out.push_back(std::move(current));
    std::erase_if(out, [](const Section& s) { return s.empty(); });
    return out;
}

/// A property line is "Label: value". The label never contains a digit, which is what keeps
/// mod wordings that happen to hold a colon out.
bool is_property_line(std::string_view line) {
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    const std::string_view label = line.substr(0, colon);
    return std::none_of(label.begin(), label.end(),
                        [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

bool is_info_line(std::string_view line) {
    return line.starts_with("{") && line.ends_with("}");
}

/// The mod-type suffix the game appends: " (implicit)", " (crafted)", … Shortens `line` and
/// returns the type; nothing when the line carries no suffix.
std::optional<data::ModType> take_mod_suffix(std::string_view& line) {
    if (line.empty() || line.back() != ')') return std::nullopt;
    const size_t open = line.rfind(" (");
    if (open == std::string_view::npos) return std::nullopt;
    const std::string_view inner = line.substr(open + 2, line.size() - open - 3);
    const std::optional<data::ModType> t = data::mod_type_from_prefix(inner);
    if (!t) return std::nullopt;
    line = trim(line.substr(0, open));
    return t;
}

/// The info line's generation words say what kind of mod follows; its own lines do not repeat
/// it, so "Master Crafted Prefix" is the only thing marking a crafted mod on an item whose
/// owner has Advanced Mod Descriptions on.
struct Generation {
    std::string_view word;
    data::ModType type;
};

data::ModType type_from_generation(std::string_view g) {
    static constexpr Generation kMap[]{
        {"Implicit", data::ModType::Implicit},   {"Enchant", data::ModType::Enchant},
        {"Fractured", data::ModType::Fractured}, {"Crafted", data::ModType::Crafted},
        {"Veiled", data::ModType::Veiled},       {"Scourge", data::ModType::Scourge},
        {"Crucible", data::ModType::Crucible},   {"Sanctum", data::ModType::Sanctum},
        {"Delve", data::ModType::Delve},         {"Ultimatum", data::ModType::Ultimatum},
    };
    for (const Generation& e : kMap)
        if (g.find(e.word) != std::string_view::npos) return e.type;
    return data::ModType::Explicit;
}

/// `{ Prefix Modifier "Urchin's" (Tier: 2) — Life, Defences }`, and the em-dash segments the
/// game appends after the tags: `{ Implicit Modifier — Critical — 20% Increased }` is a mod a
/// catalyst scaled, and the roll printed on the line is the *unscaled* one. A plain " - "
/// separates them just as well — see the clipboard encoding note below.
bool parse_info_line(std::string_view line, Modifier& out) {
    if (!is_info_line(line)) return false;
    line = trim(line.substr(1, line.size() - 2));

    constexpr std::string_view kEmDash = "\xe2\x80\x94"; // U+2014, what the game prints
    // ...and " - " is the same separator after a Latin-1 round trip. Wine publishes the
    // Windows clipboard's CF_UNICODETEXT as the X11 UTF-8 targets and CF_TEXT as `STRING`,
    // and right after a copy PoE has often rendered only the latter, so `clipboard_text`
    // falls back to it and every em dash arrives as a plain hyphen. Without this the whole
    // line stays one blob: no tier, no tags, and `generation` never ends in "Prefix", so the
    // affix is unknown — the same item then prices differently depending on which of the two
    // the poll happened to catch.
    const auto rfind_separator = [&kEmDash](std::string_view s) -> std::pair<size_t, size_t> {
        const size_t em = s.rfind(kEmDash);
        const size_t ascii = s.rfind(" - ");
        if (em != std::string_view::npos && (ascii == std::string_view::npos || em > ascii))
            return {em, kEmDash.size()};
        if (ascii != std::string_view::npos) return {ascii + 1, 1}; // the hyphen itself
        return {std::string_view::npos, 0};
    };
    while (true) {
        const auto [dash, dash_len] = rfind_separator(line);
        if (dash == std::string_view::npos) break;
        const std::string_view seg = trim(line.substr(dash + dash_len));
        line = trim(line.substr(0, dash));
        if (seg.ends_with("Increased") || seg.ends_with("Reduced")) {
            out.roll_incr = first_number(seg).value_or(0);
            if (seg.ends_with("Reduced")) out.roll_incr = -out.roll_incr;
            continue;
        }
        // Tags, and the last segment reached is the leftmost, so prepend to keep their order.
        std::vector<std::string> tags;
        for (std::string_view rest = seg; !rest.empty();) {
            const size_t comma = rest.find(',');
            tags.emplace_back(trim(rest.substr(0, comma)));
            if (comma == std::string_view::npos) break;
            rest = rest.substr(comma + 1);
        }
        out.tags.insert(out.tags.begin(), tags.begin(), tags.end());
    }

    // Trailing parentheticals, right to left: (Tier: 2), (Rank: 1), an eldritch implicit's
    // (Lesser). Anything unrecognised is kept for display rather than left on `generation`.
    while (line.ends_with(")")) {
        const size_t open = line.rfind('(');
        if (open == std::string_view::npos) break;
        const std::string_view inner = line.substr(open + 1, line.size() - open - 2);
        if (inner.starts_with("Tier: ")) out.tier = first_int(inner).value_or(0);
        else if (inner.starts_with("Rank: ")) out.rank = first_int(inner).value_or(0);
        else if (inner.find(' ') == std::string_view::npos) out.qualifier = std::string(inner);
        else break;
        line = trim(line.substr(0, open));
    }

    if (line.ends_with("\"")) {
        if (const size_t open = line.rfind('"', line.size() - 2); open != std::string_view::npos) {
            out.affix_name = std::string(line.substr(open + 1, line.size() - open - 2));
            line = trim(line.substr(0, open));
        }
    }

    if (line.ends_with("Modifier")) line = trim(line.substr(0, line.size() - 8));
    out.generation = std::string(line);
    if (out.generation.ends_with("Prefix")) out.affix = Affix::Prefix;
    else if (out.generation.ends_with("Suffix")) out.affix = Affix::Suffix;
    out.advanced = true;
    return true;
}

void parse_header(const Section& sec, Item& it) {
    for (const std::string& line : sec) {
        std::string_view v = line;
        if (v.starts_with("Item Class: ")) {
            it.item_class = std::string(trim(v.substr(12)));
        } else if (v.starts_with("Rarity: ")) {
            it.rarity = rarity_from_string(trim(v.substr(8)));
        } else if (it.name.empty() && it.base_type.empty()) {
            it.base_type = line;
        } else if (it.name.empty()) {
            // Two name lines: the first was the item's own name after all.
            it.name = std::move(it.base_type);
            it.base_type = line;
        } else {
            it.unparsed.push_back(line);
        }
    }
    // A normal item with quality is printed as "Superior <base>"; the affix is not part of
    // the base's name and would fail every lookup.
    if (it.rarity == Rarity::Normal && it.base_type.starts_with("Superior "))
        it.base_type.erase(0, 9);
    it.base_name = it.base_type;
}

void parse_requirements(const Section& sec, Item& it) {
    for (const std::string& line : sec) {
        if (line == "Requirements:") continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string_view label = trim(std::string_view(line).substr(0, colon));
        const std::optional<int> v = first_int(std::string_view(line).substr(colon + 1));
        if (!v) continue;
        if (label == "Level") it.req.level = v;
        else if (label == "Str") it.req.str = v;
        else if (label == "Dex") it.req.dex = v;
        else if (label == "Int") it.req.intelligence = v;
    }
}

void parse_sockets(std::string_view line, Item& it) {
    it.sockets = std::string(trim(line.substr(line.find(':') + 1)));
    it.socket_count = static_cast<int>(std::count_if(it.sockets.begin(), it.sockets.end(),
                                                     [](char c) {
                                                         return std::isalpha(
                                                                    static_cast<unsigned char>(c)) != 0;
                                                     }));
}

void take_typed_property(const Property& p, Item& it) {
    const std::string& v = p.value;
    if (p.label == "Quality") {
        it.quality = first_int(v);
    } else if (p.label.starts_with("Quality (") && p.label.ends_with(")")) {
        // Catalyst quality on jewellery: "Quality (Critical Modifiers): +20%".
        it.quality = first_int(v);
        it.quality_kind = p.label.substr(9, p.label.size() - 10);
    } else if (p.label == "Physical Damage") {
        it.physical = parse_damage(v);
        if (it.physical) it.physical->augmented = p.augmented;
    } else if (p.label == "Chaos Damage") {
        it.chaos = parse_damage(v);
        if (it.chaos) {
            it.chaos->element = Element::Chaos;
            it.chaos->augmented = p.augmented;
        }
    } else if (p.label == "Elemental Damage") {
        // "23-42, 3-50" — one entry per element, but the line never says which.
        std::string_view rest = v;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            if (std::optional<DamageRange> d = parse_damage(rest.substr(0, comma))) {
                d->augmented = p.augmented;
                it.elemental.push_back(*d);
            }
            if (comma == std::string_view::npos) break;
            rest = rest.substr(comma + 1);
        }
    } else if (p.label == "Critical Strike Chance") {
        it.crit_chance = first_number(v);
    } else if (p.label == "Attacks per Second") {
        it.attacks_per_second = first_number(v);
    } else if (p.label == "Armour") {
        it.armour = first_int(v);
    } else if (p.label == "Evasion Rating") {
        it.evasion = first_int(v);
    } else if (p.label == "Energy Shield") {
        it.energy_shield = first_int(v);
    } else if (p.label == "Ward") {
        it.ward = first_int(v);
    } else if (p.label == "Chance to Block" || p.label == "Block chance") {
        it.block = first_int(v);
    } else if (p.label == "Item Level") {
        it.item_level = first_int(v);
    }
}

void parse_properties(const Section& sec, Item& it) {
    for (const std::string& line : sec) {
        if (data::is_reminder_text(line) && !it.properties.empty()) {
            // The buff a utility flask grants brings its own reminder text ("(Onslaught grants
            // …)"), which the game keeps out of the tooltip the way it does for a modifier.
            it.properties.back().reminder.push_back(line);
            continue;
        }
        if (!is_property_line(line)) {
            // The property block's leading prose line is the item's type ("Bow", or a gem's
            // tag list) — never one carrying a number, or an unquality flask's own "Lasts 6
            // Seconds" becomes its type. Later prose is a flask's own effect when it reads
            // like a mod, and one of the flask's stats ("Lasts 7.20 Seconds") when it does not.
            if (it.type_line.empty() && it.properties.empty() && !first_number(line)) {
                it.type_line = line;
            } else if (first_number(line) && !std::isalpha(static_cast<unsigned char>(line[0]))) {
                it.inherent_lines.push_back(line);
            } else {
                Property p;
                p.value = strip_value_annotations(line, &p.augmented);
                it.properties.push_back(std::move(p));
            }
            continue;
        }
        const size_t colon = line.find(':');
        Property p;
        p.label = std::string(trim(std::string_view(line).substr(0, colon)));
        p.value = strip_value_annotations(std::string_view(line).substr(colon + 1), &p.augmented);
        p.num = first_number(p.value);
        take_typed_property(p, it);
        it.properties.push_back(std::move(p));
    }
}

void parse_flags(const Section& sec, Item& it) {
    for (const std::string& line : sec) {
        if (line == "Corrupted") it.corrupted = true;
        else if (line == "Unidentified") it.identified = false;
        else if (line == "Mirrored") it.mirrored = true;
        else if (line == "Split") it.split = true;
        else if (line == "Synthesised Item") it.synthesised = true;
        else if (line == "Fractured Item") it.fractured_item = true;
        else if (line == "Veiled") it.veiled = true;
        else if (line == "Unmodifiable") it.unmodifiable = true;
        else if (const std::optional<Influence> i = influence_from_line(line))
            it.influences.push_back(*i);
    }
}

bool is_flag_line(const std::string& line) {
    return std::find(kFlagLines.begin(), kFlagLines.end(), line) != kFlagLines.end() ||
           influence_from_line(line).has_value();
}

bool is_flag_section(const Section& sec) {
    return std::all_of(sec.begin(), sec.end(), is_flag_line);
}

bool is_cosmetic_section(const Section& sec) {
    return std::all_of(sec.begin(), sec.end(), [](const std::string& line) {
        return line.starts_with("Has ") && line.ends_with(" Effect");
    });
}

/// Prose: flavour text, an item's own description, or the usage note at the bottom. A modifier
/// all but always opens with its roll, which is what keeps a scourge mod — printed in its own
/// section *after* the explicits, exactly where flavour text would be — out of here.
bool is_prose_section(const Section& sec) {
    return std::none_of(sec.begin(), sec.end(), [](const std::string& line) {
        if (line.empty()) return false;
        if (std::isdigit(static_cast<unsigned char>(line.front()))) return true;
        // A sign only counts with a number behind it: flavour text is attributed with an em
        // dash the game writes as a hyphen — "-Rumi of the Vaal".
        return (line.front() == '+' || line.front() == '-') && line.size() > 1 &&
               std::isdigit(static_cast<unsigned char>(line[1])) != 0;
    });
}

/// The usage note the game prints under the flavour text: "Right click to drink…", "Place into
/// an item socket…". It is prose in the same position as flavour text, so its wording is the
/// only thing that tells them apart.
bool is_help_section(const Section& sec) {
    static constexpr std::array<std::string_view, 5> kNeedles{
        "Right click", "Shift click", "Place into an item socket", "Map Device",
        "Can be used in a personal Map Device"};
    return std::any_of(sec.begin(), sec.end(), [](const std::string& line) {
        return std::any_of(kNeedles.begin(), kNeedles.end(), [&](std::string_view n) {
            return line.find(n) != std::string::npos;
        });
    });
}

/// Decisive evidence that a section is modifiers: an Advanced Mod Descriptions info line, or a
/// line carrying the game's own mod-type suffix. Either outranks every prose heuristic.
bool looks_like_mods(const Section& sec) {
    return std::any_of(sec.begin(), sec.end(), [](const std::string& line) {
        std::string_view v = line;
        return is_info_line(line) || take_mod_suffix(v).has_value();
    });
}

/// Prose that belongs at the bottom of the tooltip rather than in the mod list.
///
/// Gear needs a positive signal, because a rare's own mods can read as prose too ("Players
/// cannot Regenerate Life"): either a quoted block, or the last prose block of a unique — the
/// one place gear prints flavour text at all. Everything else (currency, cards, gems, quest
/// items) describes itself in prose from top to bottom.
bool is_bottom_prose(const Item& it, const Section& sec, size_t index, size_t flavour_at) {
    if (sec.front().starts_with("\"")) return true;
    if (!it.is_gear()) return true;
    return index == flavour_at && it.rarity == Rarity::Unique && !it.mods.empty();
}

void parse_mod_section(const Section& sec, data::ModType fallback, Item& it) {
    // The mod an info line opened, so its continuation lines land on the same Modifier.
    // An index, not a pointer: pushing the next mod reallocates.
    size_t open = static_cast<size_t>(-1);

    for (const std::string& line : sec) {
        Modifier info;
        if (parse_info_line(line, info)) {
            info.type = type_from_generation(info.generation);
            it.mods.push_back(std::move(info));
            open = it.mods.size() - 1;
            continue;
        }
        if (data::is_reminder_text(line)) {
            if (!it.mods.empty()) it.mods.back().reminder.push_back(line);
            continue;
        }
        std::string_view text = line;
        const std::optional<data::ModType> suffix = take_mod_suffix(text);
        if (open != static_cast<size_t>(-1)) {
            // Under an info line every following line belongs to that one affix — that is
            // how a hybrid mod is recognised without guessing.
            Modifier& m = it.mods[open];
            if (suffix) m.type = *suffix;
            m.lines.emplace_back(text);
            continue;
        }
        Modifier m;
        m.type = suffix.value_or(fallback);
        m.lines.emplace_back(text);
        it.mods.push_back(std::move(m));
    }
}

/// Colour the elemental damage entries. The property line lists them in the game's fixed
/// fire, cold, lightning order but never names them, so the mods have to say which are there.
void infer_elemental_kinds(Item& it) {
    if (it.elemental.empty()) return;
    std::vector<Element> present;
    for (const Element e : {Element::Fire, Element::Cold, Element::Lightning}) {
        const std::string_view needle = e == Element::Fire      ? "Fire Damage"
                                       : e == Element::Cold    ? "Cold Damage"
                                                               : "Lightning Damage";
        for (const Modifier& m : it.mods) {
            bool hit = false;
            for (const std::string& l : m.lines)
                hit = hit || (l.starts_with("Adds ") && l.find(needle) != std::string::npos);
            if (hit) {
                present.push_back(e);
                break;
            }
        }
    }
    if (present.size() != it.elemental.size()) return; // cannot say; leave them uncoloured
    for (size_t i = 0; i < present.size(); ++i) it.elemental[i].element = present[i];
}

} // namespace

bool looks_like_item(std::string_view text) {
    return text.find("Item Class:") != std::string_view::npos ||
           text.find("Rarity:") != std::string_view::npos;
}

std::optional<Item> parse_item(std::string_view clipboard) {
    if (!looks_like_item(clipboard)) return std::nullopt;
    const std::vector<Section> sections = split_sections(clipboard);
    if (sections.empty()) return std::nullopt;

    Item it;
    parse_header(sections[0], it);
    if (it.rarity == Rarity::Unknown && it.item_class.empty()) return std::nullopt;

    // Flavour text is the last section that is not a flag, a note, a cosmetic effect or the
    // usage note — the note sits *under* the flavour text, which is why it is skipped here
    // rather than simply taking the last section.
    size_t flavour_at = 0;
    for (size_t i = sections.size(); i-- > 1;) {
        const Section& s = sections[i];
        if (is_flag_section(s) || is_cosmetic_section(s) || is_help_section(s) ||
            s.front().starts_with("Note:"))
            continue;
        if (is_prose_section(s) && !looks_like_mods(s)) flavour_at = i;
        break;
    }

    bool props_seen = false;
    std::vector<size_t> mod_sections; // for the flask enchant rule below

    for (size_t i = 1; i < sections.size(); ++i) {
        const Section& sec = sections[i];
        const std::string& first = sec.front();

        if (is_flag_section(sec)) {
            parse_flags(sec, it);
        } else if (first == "Requirements:") {
            parse_requirements(sec, it);
        } else if (first.starts_with("Sockets:")) {
            parse_sockets(first, it);
        } else if (first.starts_with("Note:")) {
            it.note = std::string(trim(std::string_view(first).substr(5)));
        } else if (is_cosmetic_section(sec)) {
            it.cosmetic_lines.insert(it.cosmetic_lines.end(), sec.begin(), sec.end());
        } else if (std::all_of(sec.begin(), sec.end(), is_property_line)) {
            parse_properties(sec, it);
            props_seen = true;
        } else if (!props_seen && i == 1 &&
                   (std::any_of(sec.begin(), sec.end(), is_property_line) || it.is_flask())) {
            // The block right after the header: properties mixed with prose — a weapon's type
            // line, a gem's tag list, a flask's own effect. A property line is the evidence
            // that this is that block at all, except on a flask: the block is always there and
            // an unquality one prints no `Label: value` line, so all of "Lasts 6 Seconds /
            // Consumes 40 of 60 Charges on use / Onslaught" read as modifiers instead.
            parse_properties(sec, it);
            props_seen = true;
        } else if (is_help_section(sec) && is_prose_section(sec)) {
            it.help_text.insert(it.help_text.end(), sec.begin(), sec.end());
        } else if (is_prose_section(sec) && !looks_like_mods(sec) &&
                   is_bottom_prose(it, sec, i, flavour_at)) {
            // A fragment says what it does in prose and then prints its verse in the same
            // shape, so the first block is the description and anything after it is flavour.
            // With only one there is no telling them apart — the Maven's Writ prints nothing
            // but its verse — and calling that the description is the harmless way round.
            if (it.is_gear() || first.starts_with("\"") ||
                (it.is_map_fragment() && !it.description.empty()))
                it.flavour_text.insert(it.flavour_text.end(), sec.begin(), sec.end());
            else if (it.description.empty())
                it.description = sec;
            else
                it.inherent_lines.insert(it.inherent_lines.end(), sec.begin(), sec.end());
        } else if (it.rarity == Rarity::Gem) {
            // A gem has no rolled modifiers: every line it prints is what the skill does, or
            // what its quality adds. Rendered, never searched.
            it.inherent_lines.insert(it.inherent_lines.end(), sec.begin(), sec.end());
        } else {
            // Influence lines sometimes come glued to the end of the last mod block instead of
            // in a section of their own, and then the last affix swallows them.
            Section mods = sec, flags;
            while (!mods.empty() && is_flag_line(mods.back())) {
                flags.insert(flags.begin(), mods.back());
                mods.pop_back();
            }
            if (!flags.empty()) parse_flags(flags, it);
            if (mods.empty()) continue;
            mod_sections.push_back(it.mods.size());
            parse_mod_section(mods, data::ModType::Explicit, it);
        }
    }

    // A flask enchantment ("Used when Charges reach full") carries no suffix and sits in its
    // own section ahead of the explicit mods, so on a flask the earlier of two unsuffixed
    // sections is the enchant.
    if (it.is_flask() && mod_sections.size() > 1) {
        const size_t end = mod_sections.back();
        for (size_t m = 0; m < end; ++m)
            if (it.mods[m].type == data::ModType::Explicit && !it.mods[m].advanced)
                it.mods[m].type = data::ModType::Enchant;
    }

    infer_elemental_kinds(it);
    return it;
}

} // namespace ppc::item
