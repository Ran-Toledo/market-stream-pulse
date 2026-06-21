#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Built at startup from AppConfig::symbols.
// After construction the hot path uses only uint32_t IDs — no allocation.
class SymbolRegistry {
public:
    static constexpr uint32_t kInvalidId = UINT32_MAX;

    explicit SymbolRegistry(const std::vector<std::string>& symbols);

    // Hot-path lookup by uppercase symbol (e.g. "BTCUSDT").
    // Linear scan over names_ — no allocation, no hash map.
    // With ~5 symbols this is faster than a hash lookup due to cache locality.
    uint32_t symbolId(std::string_view sv) const;

    // Returns empty string if id is out of range.
    const std::string& symbolName(uint32_t id) const;

    uint32_t symbolCount() const { return static_cast<uint32_t>(names_.size()); }

private:
    std::vector<std::string> names_;  // names_[id] == uppercase symbol

    static const std::string kEmpty;
};
