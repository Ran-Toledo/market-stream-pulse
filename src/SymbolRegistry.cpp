#include "SymbolRegistry.h"
#include <algorithm>
#include <cctype>

const std::string SymbolRegistry::kEmpty;

SymbolRegistry::SymbolRegistry(const std::vector<std::string>& symbols) {
    names_.reserve(symbols.size());
    lookup_.reserve(symbols.size() * 2);

    for (const auto& s : symbols) {
        // Canonical form: uppercase
        std::string upper = s;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c){ return std::toupper(c); });
        uint32_t id = static_cast<uint32_t>(names_.size());
        names_.push_back(upper);
        // Store both uppercase and lowercase so lookup works either way
        lookup_[upper] = id;
        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        lookup_[lower] = id;
    }
}

uint32_t SymbolRegistry::symbolId(const std::string& symbol) const {
    auto it = lookup_.find(symbol);
    return it != lookup_.end() ? it->second : kInvalidId;
}

uint32_t SymbolRegistry::symbolIdCI(const char* data, size_t len) const {
    // Avoid constructing std::string on the hot path — build a temporary
    // lowercase key on the stack for short symbols (max 20 chars is safe).
    if (len == 0 || len > 32) return kInvalidId;
    char buf[33];
    for (size_t i = 0; i < len; ++i)
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(data[i])));
    buf[len] = '\0';
    // std::unordered_map::find with std::string key still constructs a string
    // here; marked as TODO to replace with a custom hash or sorted array + bsearch.
    std::string key(buf, len);
    auto it = lookup_.find(key);
    return it != lookup_.end() ? it->second : kInvalidId;
}

const std::string& SymbolRegistry::symbolName(uint32_t id) const {
    if (id >= names_.size()) return kEmpty;
    return names_[id];
}
