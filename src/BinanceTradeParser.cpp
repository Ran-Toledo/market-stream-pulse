#include "BinanceTradeParser.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

BinanceTradeParser::BinanceTradeParser(const SymbolRegistry& registry)
    : registry_(registry)
{}

bool BinanceTradeParser::parse(const RawMessage& raw, TradeEvent& out) const {
    // NOTE: json::parse() may allocate internally — known MVP limitation.
    //       TODO: replace with simdjson::ondemand for zero-copy parsing.
    try {
        auto j = json::parse(raw.data, raw.data + raw.length);

        // Combined stream wrapper: {"stream":"btcusdt@trade","data":{...}}
        const auto& data = j.at("data");

        // Validate event type
        if (data.value("e", "") != "trade") return false;

        // Symbol lookup — uses CI lookup to avoid case mismatch
        const std::string& sym = data.at("s").get_ref<const std::string&>();
        uint32_t sid = registry_.symbolIdCI(sym.data(), sym.size());
        if (sid == SymbolRegistry::kInvalidId) return false;

        out.feedId       = raw.feedId;
        out.symbolId     = sid;
        out.eventTimeMs  = data.at("E").get<int64_t>();
        out.tradeTimeMs  = data.at("T").get<int64_t>();
        out.tradeId      = data.at("t").get<int64_t>();
        // p and q are JSON strings per Binance API spec
        out.price        = std::stod(data.at("p").get_ref<const std::string&>());
        out.quantity     = std::stod(data.at("q").get_ref<const std::string&>());
        out.buyerIsMaker = data.value("m", false);
        out.receiveTime  = raw.receiveTime;
        out.parsedTime   = std::chrono::steady_clock::now();

        return true;
    } catch (const std::exception&) {
        return false;
    }
}
