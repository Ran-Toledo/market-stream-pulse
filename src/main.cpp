#include "AppConfig.h"
#include "BinanceTradeParser.h"
#include "BinanceWebSocketClient.h"
#include "BoundedBlockingQueue.h"
#include "ConsoleReporter.h"
#include "Metrics.h"
#include "RawMessage.h"
#include "RawMessagePool.h"
#include "SymbolRegistry.h"
#include "TradeAggregator.h"
#include "TradeEvent.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Graceful shutdown via Ctrl+C
// ---------------------------------------------------------------------------
static std::atomic<bool> g_shutdown{false};

static void signalHandler(int) {
    g_shutdown.store(true, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Parser worker loop
// ---------------------------------------------------------------------------
static void parserWorkerLoop(
    BoundedBlockingQueue<RawMessage*>& rawQueue,
    BoundedBlockingQueue<TradeEvent>&  parsedQueue,
    RawMessagePool&                    pool,
    const BinanceTradeParser&          parser,
    PipelineMetrics&                   metrics)
{
    RawMessage* msg = nullptr;
    while (rawQueue.pop(msg)) {
        if (!msg) continue;

        auto parseStart = std::chrono::steady_clock::now();

        TradeEvent event;
        bool ok = parser.parse(*msg, event);

        // Always return buffer to pool — even on parse failure.
        pool.release(msg);
        msg = nullptr;

        if (!ok) {
            metrics.parseErrors.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Record parse latency
        auto parseEnd = std::chrono::steady_clock::now();
        double parseUs = std::chrono::duration<double, std::micro>(
            parseEnd - parseStart).count();
        metrics.parseLatency.record(parseUs);

        metrics.parsedMessages.fetch_add(1, std::memory_order_relaxed);

        if (!parsedQueue.push(std::move(event))) {
            metrics.droppedParsedQueueFull.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------
    AppConfig cfg;
    // Override defaults here if desired.
    // cfg.parserWorkerCount = 4;

    std::printf("=== Market Stream Pulse ===\n");
    std::printf("Symbols: ");
    for (auto& s : cfg.symbols) std::printf("%s ", s.c_str());
    std::printf("\nBuffer pool: %u x %u bytes\n",
        cfg.rawMessageBufferCount, cfg.rawMessageMaxSize);
    std::printf("Raw queue: %u  Parsed queue: %u  Workers: %u\n\n",
        cfg.rawQueueCapacity, cfg.parsedQueueCapacity, cfg.parserWorkerCount);

    // ------------------------------------------------------------------
    // Startup — all allocations happen here, not during runtime
    // ------------------------------------------------------------------
    SymbolRegistry registry(cfg.symbols);

    PipelineMetrics metrics;

    RawMessagePool pool(cfg.rawMessageBufferCount);

    BoundedBlockingQueue<RawMessage*> rawQueue(cfg.rawQueueCapacity);
    BoundedBlockingQueue<TradeEvent>  parsedQueue(cfg.parsedQueueCapacity);

    BinanceTradeParser parser(registry);

    TradeAggregator aggregator(cfg, registry, rawQueue, parsedQueue, metrics);
    ConsoleReporter  reporter(aggregator);

    BinanceWebSocketClient wsClient(cfg, pool, rawQueue, metrics);

    // ------------------------------------------------------------------
    // Start threads
    // ------------------------------------------------------------------
    aggregator.start();

    std::vector<std::thread> parserThreads;
    parserThreads.reserve(cfg.parserWorkerCount);
    for (uint32_t i = 0; i < cfg.parserWorkerCount; ++i) {
        parserThreads.emplace_back(parserWorkerLoop,
            std::ref(rawQueue),
            std::ref(parsedQueue),
            std::ref(pool),
            std::ref(parser),
            std::ref(metrics));
    }

    wsClient.start();

    // ------------------------------------------------------------------
    // Reporting loop — runs on main thread
    // ------------------------------------------------------------------
    auto nextReport = std::chrono::steady_clock::now() +
                      std::chrono::seconds(cfg.reportIntervalSeconds);

    while (!g_shutdown.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();

        if (now >= nextReport) {
            reporter.report();
            nextReport += std::chrono::seconds(cfg.reportIntervalSeconds);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // ------------------------------------------------------------------
    // Graceful shutdown
    // ------------------------------------------------------------------
    std::printf("\n[main] Shutting down...\n");

    wsClient.stop();

    rawQueue.close();
    for (auto& t : parserThreads) t.join();

    parsedQueue.close();
    aggregator.stop();

    std::printf("[main] Done.\n");
    return 0;
}
