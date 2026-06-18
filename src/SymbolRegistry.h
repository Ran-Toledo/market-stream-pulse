#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Built at startup from AppConfig::symbols.
// After construction the hot path uses only uint32_t IDs — no allocation.
class SymbolRegistry {
public:
    static constexpr uint32_t kInvalidId = UINT32_MAX;

    explicit SymbolRegistry(const std::vector<std::string>& symbols);

    // Hot-path lookup: accepts "btcusdt" or "BTCUSDT" via string_view.
    // Uses heterogeneous hash — no std::string constructed on the hot path.
    uint32_t symbolIdCI(std::string_view sv) const;

    // Returns empty string if id is out of range.
    const std::string& symbolName(uint32_t id) const;

    uint32_t symbolCount() const { return static_cast<uint32_t>(names_.size()); }

private:
    // Transparent hash: allows find() with std::string_view without constructing std::string.
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };

    std::vector<std::string> names_;
    std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>> lookup_;

    static const std::string kEmpty;
};
