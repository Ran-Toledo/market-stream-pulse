#pragma once
#include "RawMessage.h"
#include "SymbolRegistry.h"
#include "TradeEvent.h"

class BinanceTradeParser {
public:
    explicit BinanceTradeParser(const SymbolRegistry& registry);

    // Parses a Binance combined stream JSON frame using simdjson ondemand.
    // Zero-copy: operates directly on RawMessage::data with no heap allocation.
    // Each parser worker thread gets its own simdjson parser via thread_local storage.
    // Returns true and fills out on success.
    bool parse(const RawMessage& raw, TradeEvent& out) const;

private:
    const SymbolRegistry& registry_;
};
