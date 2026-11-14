// Every captured item through parse, resolve, plan and query, against one data bundle, as text.
//
// The way to see what a data build changes before it ships: dump against the release bundle
// and against a local build, then diff the two. Not a test — it asserts nothing, the diff is
// the point — so it is a tool and never part of ctest.
//
//   plan_dump <bundle dir> <capture>...
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "data/game_data.hpp"
#include "item/derive.hpp"
#include "item/plan.hpp"
#include "item/resolve.hpp"
#include "trade/query.hpp"

namespace fs = std::filesystem;
using namespace ppc;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: plan_dump <bundle dir> <capture>...\n";
        return 2;
    }
    std::string err;
    const auto gd = data::GameData::open(argv[1], "en", &err);
    if (!gd) {
        std::cerr << "cannot open " << argv[1] << ": " << err << "\n";
        return 1;
    }
    for (int i = 2; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::cout << "=== " << fs::path(argv[i]).filename().string() << "\n";
        // The bundle's own lexicon, which is the whole reason this runs per bundle.
        std::optional<item::Item> it = item::parse_item(ss.str(), gd->lexicon());
        if (!it) {
            std::cout << "not an item\n";
            continue;
        }
        item::resolve_item(*gd, *it);
        for (const std::string& l : it->unparsed) std::cout << "unparsed: " << l << "\n";
        for (const item::Modifier& m : it->mods)
            if (!m.match || !m.match->stat)
                std::cout << "unmatched: " << (m.lines.empty() ? "" : m.lines.front()) << "\n";
        const item::SearchPlan p = item::build_plan(*gd, *it, item::derive(gd.get(), *it));
        std::cout << "strategy: " << static_cast<int>(p.strategy) << "\n";
        for (const std::string& n : p.notes) std::cout << "note: " << n << "\n";
        std::cout << "query: " << trade::build_query(p) << "\n";
    }
    return 0;
}
