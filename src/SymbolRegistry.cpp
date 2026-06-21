#include "SymbolRegistry.h"
#include <algorithm>
#include <cctype>

const std::string SymbolRegistry::kEmpty;

SymbolRegistry::SymbolRegistry(const std::vector<std::string>& symbols) {
    names_.reserve(symbols.size());
    for (const auto& s : symbols) {
        std::string upper = s;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c){ return std::toupper(c); });
        names_.push_back(std::move(upper));
    }
}

uint32_t SymbolRegistry::symbolId(std::string_view sv) const {
    // std::string::operator==(std::string_view) is C++17, zero-allocation.
    // Linear scan is O(n) but n <= ~10 symbols — faster than a hash map at this scale.
    for (uint32_t i = 0; i < static_cast<uint32_t>(names_.size()); ++i) {
        if (names_[i] == sv) return i;
    }
    return kInvalidId;
}

const std::string& SymbolRegistry::symbolName(uint32_t id) const {
    if (id >= names_.size()) return kEmpty;
    return names_[id];
}
