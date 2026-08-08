#pragma once

#include "data/lexicon.hpp"
#include "item/item.hpp"

/// Parse a capture with the compiled-in English vocabulary.
///
/// `parse_item` takes a lexicon on purpose and has no default — the language is the one input
/// it cannot infer, so the app has to state it. Every capture in `tests/data` is from an
/// English client, so the tests state it once here rather than at three hundred call sites.
namespace ppc::item {

inline std::optional<Item> parse_item_en(std::string_view clipboard) {
    return parse_item(clipboard, data::Lexicon::english());
}

inline bool looks_like_item_en(std::string_view text) {
    return looks_like_item(text, data::Lexicon::english());
}

} // namespace ppc::item
