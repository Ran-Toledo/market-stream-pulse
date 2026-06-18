#pragma once
#include <chrono>
#include <cstdint>

// Normalised internal event — no Binance-specific field names beyond this point.
// No std::string to avoid heap allocation on the hot path.
struct TradeEvent {
    uint32_t feedId     = 0;
    uint32_t symbolId   = 0;     // index into SymbolRegistry
    int64_t  eventTimeMs = 0;    // exchange event timestamp (ms)
    int64_t  tradeTimeMs = 0;    // exchange trade timestamp (ms)
    int64_t  tradeId    = 0;
    double   price      = 0.0;
    double   quantity   = 0.0;
    bool     buyerIsMaker = false;

    std::chrono::steady_clock::time_point receiveTime;  // set by WebSocket client
    std::chrono::steady_clock::time_point parsedTime;   // set by parser worker
};
