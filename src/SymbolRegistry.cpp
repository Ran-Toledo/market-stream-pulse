#include "SymbolRegistry.h"
#include <algorithm>
#include <cctype>

const std::string SymbolRegistry::kEmpty;

SymbolRegistry::SymbolRegistry(const std::vector<std::string>& symbols) {
    names_.reserve(symbols.size());
    lookup_.reserve(symbols.size() * 2);

    for (const auto& s : symbols) {
        uint32_t id = static_cast<uint32_t>(names_.size());

        std::string upper = s;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c){ return std::toupper(c); });

        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        names_.push_back(upper);
        lookup_[upper] = id;
        lookup_[lower] = id;
    }
}

uint32_t SymbolRegistry::symbolIdCI(std::string_view sv) const {
    if (sv.empty() || sv.size() > 32) return kInvalidId;

    // Lowercase into a stack buffer — no heap allocation.
    char buf[33];
    for (size_t i = 0; i < sv.size(); ++i)
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(sv[i])));

    // Heterogeneous find: StringHash and std::equal_to<> accept string_view directly,
    // so no std::string is constructed here.
    auto it = lookup_.find(std::string_view(buf, sv.size()));
    return it != lookup_.end() ? it->second : kInvalidId;
}

const std::string& SymbolRegistry::symbolName(uint32_t id) const {
    if (id >= names_.size()) return kEmpty;
    return names_[id];
}
