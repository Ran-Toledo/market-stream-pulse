#pragma once
#include "AppConfig.h"
#include "BoundedBlockingQueue.h"
#include "Metrics.h"
#include "RawMessage.h"
#include "SymbolRegistry.h"
#include "TradeEvent.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

class TradeAggregator {
public:
    TradeAggregator(const AppConfig& cfg,
                    const SymbolRegistry& registry,
                    BoundedBlockingQueue<RawMessage*>& rawQueue,
                    BoundedBlockingQueue<TradeEvent>& parsedQueue,
                    PipelineMetrics& metrics);

    // Starts the aggregator thread — call once.
    void start();

    // Signals the aggregator to stop and waits for it.
    void stop();

    // Returns a copy of the latest published snapshot.
    MetricsSnapshot latestSnapshot() const;

private:
    void run();
    void publishSnapshot(double intervalSecs);

    const AppConfig& cfg_;
    const SymbolRegistry& registry_;
    BoundedBlockingQueue<RawMessage*>& rawQueue_;
    BoundedBlockingQueue<TradeEvent>& parsedQueue_;
    PipelineMetrics& metrics_;

    struct SymbolState {
        uint64_t totalTrades      = 0;
        uint64_t intervalTrades   = 0;
        double   lastPrice        = 0.0;
        double   intervalVolume   = 0.0;
        double   intervalPriceSum = 0.0;
        double   minPrice         = 0.0;
        double   maxPrice         = 0.0;
    };

    // Preallocated per-symbol state — owned by the aggregator thread exclusively.
    std::vector<SymbolState> symbolState_;

    // Snapshot double buffer — aggregator writes, reporter reads.
    mutable std::mutex snapshotMutex_;
    MetricsSnapshot publishedSnapshot_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    // Timestamps for interval tracking
    std::chrono::steady_clock::time_point lastReport_;
    uint64_t prevRawMessages_  = 0;
    uint64_t prevRawBytes_     = 0;
    uint64_t prevParsed_       = 0;
    uint64_t prevParseErrors_  = 0;
    uint64_t prevConsumed_     = 0;
};
