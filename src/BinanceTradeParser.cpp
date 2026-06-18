#include "BinanceTradeParser.h"
#include <charconv>
#include <simdjson.h>

// One parser per thread — simdjson::ondemand::parser is not thread-safe.
// It reuses its internal buffer across calls, so after the first parse
// there are no further allocations from the parser itself.
static thread_local simdjson::ondemand::parser tl_parser;

BinanceTradeParser::BinanceTradeParser(const SymbolRegistry& registry)
    : registry_(registry)
{}

bool BinanceTradeParser::parse(const RawMessage& raw, TradeEvent& out) const {
    // simdjson ondemand requires the buffer to have SIMDJSON_PADDING bytes of
    // readable space after raw.length.  RawMessage::data is MAX_MSG_SIZE bytes,
    // so as long as raw.length + SIMDJSON_PADDING <= MAX_MSG_SIZE we are safe.
    if (raw.length + simdjson::SIMDJSON_PADDING > MAX_MSG_SIZE) return false;

    simdjson::ondemand::document doc;
    // iterate() operates on raw.data in-place — zero copy, no allocation.
    if (tl_parser.iterate(raw.data, raw.length, MAX_MSG_SIZE).get(doc)) return false;

    // Combined stream wrapper: {"stream":"btcusdt@trade","data":{...}}
    simdjson::ondemand::object data;
    if (doc["data"].get(data)) return false;

    // Validate event type
    std::string_view event_type;
    if (data["e"].get(event_type) || event_type != "trade") return false;

    // Exchange event timestamp
    int64_t event_time;
    if (data["E"].get(event_time)) return false;

    // Symbol — string_view into the raw buffer, no copy
    std::string_view sym;
    if (data["s"].get(sym)) return false;
    uint32_t sid = registry_.symbolId(sym);
    if (sid == SymbolRegistry::kInvalidId) return false;

    // Trade ID
    int64_t trade_id;
    if (data["t"].get(trade_id)) return false;

    // Price and quantity are JSON strings per Binance spec
    std::string_view price_sv, qty_sv;
    if (data["p"].get(price_sv)) return false;
    if (data["q"].get(qty_sv)) return false;

    double price = 0.0, quantity = 0.0;
    if (std::from_chars(price_sv.data(), price_sv.data() + price_sv.size(), price).ec != std::errc{})
        return false;
    if (std::from_chars(qty_sv.data(), qty_sv.data() + qty_sv.size(), quantity).ec != std::errc{})
        return false;

    // Trade timestamp
    int64_t trade_time;
    if (data["T"].get(trade_time)) return false;

    // Buyer-is-maker flag
    bool buyer_is_maker = false;
    data["m"].get(buyer_is_maker); // optional — ignore error

    out.feedId       = raw.feedId;
    out.symbolId     = sid;
    out.eventTimeMs  = event_time;
    out.tradeTimeMs  = trade_time;
    out.tradeId      = trade_id;
    out.price        = price;
    out.quantity     = quantity;
    out.buyerIsMaker = buyer_is_maker;
    out.receiveTime  = raw.receiveTime;
    out.parsedTime   = std::chrono::steady_clock::now();

    return true;
}
