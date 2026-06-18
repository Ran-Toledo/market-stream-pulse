#pragma once
#include "RawMessage.h"
#include "SymbolRegistry.h"
#include "TradeEvent.h"
#include <cstdint>

class BinanceTradeParser {
public:
    explicit BinanceTradeParser(const SymbolRegistry& registry);

    // Parses a Binance combined stream JSON frame.
    // Returns true and fills out on success.
    // NOTE: nlohmann::json may internally allocate during parsing.
    //       TODO: replace with simdjson on-demand or a hand-written field extractor.
    bool parse(const RawMessage& raw, TradeEvent& out) const;

private:
    const SymbolRegistry& registry_;
};
