#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Built at startup from AppConfig::symbols.
// After construction the hot path uses only uint32_t IDs — no allocation.
class SymbolRegistry {
public:
    static constexpr uint32_t kInvalidId = UINT32_MAX;

    // Populates registry from the ordered list; IDs are 0-based indices.
    explicit SymbolRegistry(const std::vector<std::string>& symbols);

    // Returns kInvalidId if not found.  O(1) hash lookup — but this
    // involves std::string hashing.  For the hot path prefer symbolIdFromData().
    uint32_t symbolId(const std::string& symbol) const;

    // Uppercase-normalised lookup: accepts "btcusdt" or "BTCUSDT".
    uint32_t symbolIdCI(const char* data, size_t len) const;

    // Returns empty string if id is out of range.
    const std::string& symbolName(uint32_t id) const;

    uint32_t symbolCount() const { return static_cast<uint32_t>(names_.size()); }

private:
    // Populated at startup; never resized.
    std::vector<std::string> names_;
    std::unordered_map<std::string, uint32_t> lookup_;

    static const std::string kEmpty;
};
