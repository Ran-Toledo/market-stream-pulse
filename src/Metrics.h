#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Simple latency accumulator — not a full histogram.
// TODO: replace with p50/p95/p99 HDR histogram for production.
struct LatencyAccumulator {
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> sumUs{0};   // microseconds
    std::atomic<uint64_t> maxUs{0};

    void record(double us) {
        auto usInt = static_cast<uint64_t>(us);
        count.fetch_add(1, std::memory_order_relaxed);
        sumUs.fetch_add(usInt, std::memory_order_relaxed);
        uint64_t prev = maxUs.load(std::memory_order_relaxed);
        while (prev < usInt &&
               !maxUs.compare_exchange_weak(prev, usInt, std::memory_order_relaxed))
        {}
    }

    // Reset and return snapshot {count, avgUs, maxUs}
    struct Snapshot { uint64_t count; double avgUs; double maxUs; };
    Snapshot swap() {
        uint64_t c = count.exchange(0, std::memory_order_relaxed);
        uint64_t s = sumUs.exchange(0, std::memory_order_relaxed);
        uint64_t m = maxUs.exchange(0, std::memory_order_relaxed);
        double avg = (c > 0) ? (static_cast<double>(s) / c) : 0.0;
        return { c, avg, static_cast<double>(m) };
    }
};

// All pipeline-level metrics.  Per-symbol metrics live in TradeAggregator.
struct PipelineMetrics {
    // --- Connection ---
    std::atomic<bool> connected{false};

    // --- Network ---
    std::atomic<uint64_t> totalRawMessages{0};
    std::atomic<uint64_t> totalRawBytes{0};
    std::atomic<uint64_t> droppedNoBuffer{0};
    std::atomic<uint64_t> droppedRawQueueFull{0};
    std::atomic<uint64_t> oversizedMessages{0};

    // --- Parser ---
    std::atomic<uint64_t> parsedMessages{0};
    std::atomic<uint64_t> parseErrors{0};
    std::atomic<uint64_t> droppedParsedQueueFull{0};

    // --- Latency ---
    LatencyAccumulator parseLatency;
    LatencyAccumulator rawQueueWait;
    LatencyAccumulator parsedQueueWait;
    LatencyAccumulator endToEndLocal;

    // Exchange lag (wall-clock difference, can be negative with clock skew)
    std::atomic<int64_t>  exchangeLagSumMs{0};
    std::atomic<uint64_t> exchangeLagCount{0};
};

// Snapshot published by the aggregator once per reporting interval.
struct SymbolSnapshot {
    std::string name;
    uint64_t tradesThisInterval  = 0;
    double   lastPrice           = 0.0;
    double   volumeThisInterval  = 0.0;
    double   minPrice            = 0.0;
    double   maxPrice            = 0.0;
    double   avgPrice            = 0.0;
};

struct MetricsSnapshot {
    // Timing
    std::chrono::steady_clock::time_point timestamp;
    double intervalSeconds = 1.0;

    // Connection
    bool connected = false;
    uint32_t feedCount   = 1;
    uint32_t symbolCount = 0;
    uint32_t parserWorkers = 0;

    // Network
    uint64_t rawMessagesPerSec  = 0;
    uint64_t rawBytesPerSec     = 0;
    uint64_t totalRawMessages   = 0;
    uint64_t oversizedMessages  = 0;
    uint64_t droppedNoBuffer    = 0;
    uint64_t droppedRawQueue    = 0;

    // Queues
    uint32_t rawQueueDepth    = 0;
    uint32_t rawQueueCapacity = 0;
    uint32_t rawQueueMaxDepth = 0;
    uint32_t parsedQueueDepth    = 0;
    uint32_t parsedQueueCapacity = 0;
    uint32_t parsedQueueMaxDepth = 0;
    uint64_t parsedEventDrops    = 0;

    // Parser
    uint64_t parsedPerSec    = 0;
    uint64_t parseErrorsPerSec = 0;
    double   avgParseLatencyUs = 0.0;
    double   maxParseLatencyUs = 0.0;

    // Processing
    uint64_t consumedPerSec         = 0;
    double   avgRawQueueWaitUs      = 0.0;
    double   avgParsedQueueWaitUs   = 0.0;
    double   avgEndToEndLocalUs     = 0.0;
    double   maxEndToEndLocalUs     = 0.0;
    double   estimatedExchangeLagMs = 0.0;

    // Per-symbol
    std::vector<SymbolSnapshot> symbols;
};
