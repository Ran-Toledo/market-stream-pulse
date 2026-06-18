#include "SymbolRegistry.h"
#include <algorithm>
#include <cctype>

const std::string SymbolRegistry::kEmpty;

SymbolRegistry::SymbolRegistry(const std::vector<std::string>& symbols) {
    names_.reserve(symbols.size());
    lookup_.reserve(symbols.size());

    for (const auto& s : symbols) {
        uint32_t id = static_cast<uint32_t>(names_.size());

        std::string upper = s;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c){ return std::toupper(c); });

        names_.push_back(upper);
        lookup_[upper] = id;
    }
}

uint32_t SymbolRegistry::symbolId(std::string_view sv) const {
    // Transparent find: StringHash and std::equal_to<> operate on string_view directly.
    // No std::string constructed, no case transformation — zero hot-path overhead.
    auto it = lookup_.find(sv);
    return it != lookup_.end() ? it->second : kInvalidId;
}

const std::string& SymbolRegistry::symbolName(uint32_t id) const {
    if (id >= names_.size()) return kEmpty;
    return names_[id];
}
