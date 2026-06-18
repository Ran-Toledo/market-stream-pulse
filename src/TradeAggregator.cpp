#include "TradeAggregator.h"
#include <algorithm>
#include <limits>
#include <thread>

TradeAggregator::TradeAggregator(const AppConfig& cfg,
                                 const SymbolRegistry& registry,
                                 BoundedBlockingQueue<RawMessage*>& rawQueue,
                                 BoundedBlockingQueue<TradeEvent>& parsedQueue,
                                 PipelineMetrics& metrics)
    : cfg_(cfg)
    , registry_(registry)
    , rawQueue_(rawQueue)
    , parsedQueue_(parsedQueue)
    , metrics_(metrics)
{
    // Preallocate per-symbol state — never resized at runtime.
    symbolState_.resize(registry.symbolCount());
    for (auto& s : symbolState_) {
        s.minPrice = std::numeric_limits<double>::max();
        s.maxPrice = std::numeric_limits<double>::lowest();
    }

    // Pre-populate snapshot symbol list so the reporter doesn't allocate.
    publishedSnapshot_.symbols.resize(registry.symbolCount());
    for (uint32_t i = 0; i < registry.symbolCount(); ++i)
        publishedSnapshot_.symbols[i].name = registry.symbolName(i);
    publishedSnapshot_.symbolCount  = registry.symbolCount();
    publishedSnapshot_.parserWorkers = cfg_.parserWorkerCount;
    publishedSnapshot_.rawQueueCapacity    = cfg_.rawQueueCapacity;
    publishedSnapshot_.parsedQueueCapacity = cfg_.parsedQueueCapacity;
}

void TradeAggregator::start() {
    running_.store(true);
    lastReport_ = std::chrono::steady_clock::now();
    thread_ = std::thread(&TradeAggregator::run, this);
}

void TradeAggregator::stop() {
    running_.store(false);
    thread_.join();
}

MetricsSnapshot TradeAggregator::latestSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return publishedSnapshot_;
}

void TradeAggregator::run() {
    TradeEvent event;

    while (running_.load(std::memory_order_relaxed)) {
        // Use a short timed pop so the reporting interval fires on time
        // even when the queue is idle (e.g. after-hours, low-rate symbols).
        bool got = parsedQueue_.pop_for(event, std::chrono::milliseconds(100));

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastReport_).count();
        if (elapsed >= cfg_.reportIntervalSeconds) {
            publishSnapshot(elapsed);
            lastReport_ = now;
            for (auto& s : symbolState_) {
                s.intervalTrades   = 0;
                s.intervalVolume   = 0.0;
                s.intervalPriceSum = 0.0;
                s.minPrice = std::numeric_limits<double>::max();
                s.maxPrice = std::numeric_limits<double>::lowest();
            }
        }

        if (!got) {
            // Timeout or queue closed — check running flag and loop.
            if (parsedQueue_.isClosed()) break;
            continue;
        }

        if (event.symbolId >= symbolState_.size()) continue;

        SymbolState& ss = symbolState_[event.symbolId];
        ++ss.totalTrades;
        ++ss.intervalTrades;
        ss.lastPrice = event.price;
        ss.intervalVolume   += event.price * event.quantity;
        ss.intervalPriceSum += event.price;
        ss.minPrice = std::min(ss.minPrice, event.price);
        ss.maxPrice = std::max(ss.maxPrice, event.price);

        // Latency measurements
        auto parsedTime = event.parsedTime;
        auto recvTime   = event.receiveTime;
        auto consumeTime = std::chrono::steady_clock::now();

        auto rawWaitUs = std::chrono::duration<double, std::micro>(
            parsedTime - recvTime).count();
        metrics_.rawQueueWait.record(rawWaitUs);

        auto parsedWaitUs = std::chrono::duration<double, std::micro>(
            consumeTime - parsedTime).count();
        metrics_.parsedQueueWait.record(parsedWaitUs);

        auto e2eUs = std::chrono::duration<double, std::micro>(
            consumeTime - recvTime).count();
        metrics_.endToEndLocal.record(e2eUs);

        // Estimated exchange lag: wall-clock now vs exchange trade timestamp.
        // Not perfectly accurate (clock sync), but useful as an indicator.
        auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t lagMs = static_cast<int64_t>(wallMs) - event.tradeTimeMs;
        metrics_.exchangeLagSumMs.fetch_add(lagMs, std::memory_order_relaxed);
        metrics_.exchangeLagCount.fetch_add(1,  std::memory_order_relaxed);
    }
}

void TradeAggregator::publishSnapshot(double intervalSecs) {
    if (intervalSecs <= 0.0) intervalSecs = 1.0;

    // Compute everything outside the lock first, then update in-place.
    // publishedSnapshot_.symbols was pre-allocated at startup — no resize needed.

    uint64_t rawMsgs   = metrics_.totalRawMessages.load(std::memory_order_relaxed);
    uint64_t rawBytes  = metrics_.totalRawBytes.load(std::memory_order_relaxed);
    uint64_t parsed    = metrics_.parsedMessages.load(std::memory_order_relaxed);
    uint64_t parseErr  = metrics_.parseErrors.load(std::memory_order_relaxed);

    uint64_t rawPerSec    = static_cast<uint64_t>((rawMsgs - prevRawMessages_) / intervalSecs);
    uint64_t bytesPerSec  = static_cast<uint64_t>((rawBytes - prevRawBytes_)   / intervalSecs);
    uint64_t parsedPerSec = static_cast<uint64_t>((parsed  - prevParsed_)      / intervalSecs);
    uint64_t errPerSec    = static_cast<uint64_t>((parseErr - prevParseErrors_) / intervalSecs);

    prevRawMessages_ = rawMsgs;
    prevRawBytes_    = rawBytes;
    prevParsed_      = parsed;
    prevParseErrors_ = parseErr;

    auto parseLat   = metrics_.parseLatency.swap();
    auto rawWait    = metrics_.rawQueueWait.swap();
    auto parsedWait = metrics_.parsedQueueWait.swap();
    auto e2e        = metrics_.endToEndLocal.swap();

    uint64_t lagCount = metrics_.exchangeLagCount.exchange(0, std::memory_order_relaxed);
    int64_t  lagSum   = metrics_.exchangeLagSumMs.exchange(0, std::memory_order_relaxed);

    uint32_t rawDepth    = rawQueue_.depth();
    uint32_t rawMaxDepth = rawQueue_.swapMaxDepthInterval();
    uint32_t pqDepth     = parsedQueue_.depth();
    uint32_t pqMaxDepth  = parsedQueue_.swapMaxDepthInterval();

    std::lock_guard<std::mutex> lock(snapshotMutex_);
    MetricsSnapshot& s = publishedSnapshot_;

    s.timestamp          = std::chrono::steady_clock::now();
    s.intervalSeconds    = intervalSecs;
    s.connected          = metrics_.connected.load(std::memory_order_relaxed);
    s.symbolCount        = registry_.symbolCount();
    s.parserWorkers      = cfg_.parserWorkerCount;
    s.feedCount          = 1;

    s.rawMessagesPerSec  = rawPerSec;
    s.rawBytesPerSec     = bytesPerSec;
    s.parsedPerSec       = parsedPerSec;
    s.parseErrorsPerSec  = errPerSec;
    s.totalRawMessages   = rawMsgs;
    s.oversizedMessages  = metrics_.oversizedMessages.load(std::memory_order_relaxed);
    s.droppedNoBuffer    = metrics_.droppedNoBuffer.load(std::memory_order_relaxed);
    s.droppedRawQueue    = metrics_.droppedRawQueueFull.load(std::memory_order_relaxed);
    s.parsedEventDrops   = metrics_.droppedParsedQueueFull.load(std::memory_order_relaxed);

    s.rawQueueCapacity    = rawQueue_.capacity();
    s.rawQueueDepth       = rawDepth;
    s.rawQueueMaxDepth    = rawMaxDepth;
    s.parsedQueueCapacity = parsedQueue_.capacity();
    s.parsedQueueDepth    = pqDepth;
    s.parsedQueueMaxDepth = pqMaxDepth;

    s.avgParseLatencyUs   = parseLat.avgUs;
    s.maxParseLatencyUs   = parseLat.maxUs;
    s.avgRawQueueWaitUs   = rawWait.avgUs;
    s.avgParsedQueueWaitUs = parsedWait.avgUs;
    s.avgEndToEndLocalUs  = e2e.avgUs;
    s.maxEndToEndLocalUs  = e2e.maxUs;
    s.consumedPerSec      = static_cast<uint64_t>(e2e.count / intervalSecs);
    s.estimatedExchangeLagMs = (lagCount > 0)
        ? (static_cast<double>(lagSum) / lagCount) : 0.0;

    // Update per-symbol fields in-place — vector already sized at startup.
    for (uint32_t i = 0; i < registry_.symbolCount(); ++i) {
        const SymbolState& ss = symbolState_[i];
        SymbolSnapshot& sym   = s.symbols[i];
        sym.tradesThisInterval = ss.intervalTrades;
        sym.lastPrice          = ss.lastPrice;
        sym.volumeThisInterval = ss.intervalVolume;
        sym.avgPrice = (ss.intervalTrades > 0)
            ? (ss.intervalPriceSum / ss.intervalTrades) : 0.0;
        sym.minPrice = (ss.minPrice == std::numeric_limits<double>::max())     ? 0.0 : ss.minPrice;
        sym.maxPrice = (ss.maxPrice == std::numeric_limits<double>::lowest())  ? 0.0 : ss.maxPrice;
    }
}
