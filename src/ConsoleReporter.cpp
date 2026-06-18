#include "ConsoleReporter.h"
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>

ConsoleReporter::ConsoleReporter(const TradeAggregator& aggregator)
    : aggregator_(aggregator)
{}

static void line(char c = '-', int n = 60) {
    std::string s(n, c);
    std::puts(s.c_str());
}

void ConsoleReporter::report() {
    MetricsSnapshot s = aggregator_.latestSnapshot();

    line('=');
    std::puts("  MarketStream Pulse  |  Metrics Report");
    line('=');

    // Connection
    std::printf("Connection:\n");
    std::printf("  status        : %s\n", s.connected ? "connected" : "connecting...");
    std::printf("  feeds         : %u\n", s.feedCount);
    std::printf("  symbols       : %u\n", s.symbolCount);
    std::printf("  parser workers: %u\n", s.parserWorkers);
    std::puts("");

    // Input
    std::printf("Input:\n");
    std::printf("  raw msgs/sec  : %llu\n", (unsigned long long)s.rawMessagesPerSec);
    std::printf("  raw bytes/sec : %llu\n", (unsigned long long)s.rawBytesPerSec);
    std::printf("  total raw msgs: %llu\n", (unsigned long long)s.totalRawMessages);
    std::printf("  oversized     : %llu\n", (unsigned long long)s.oversizedMessages);
    std::printf("  dropped/no buf: %llu\n", (unsigned long long)s.droppedNoBuffer);
    std::printf("  dropped/rq full:%llu\n", (unsigned long long)s.droppedRawQueue);
    std::puts("");

    // Queues
    std::printf("Queues:\n");
    std::printf("  raw queue     : %u / %u  (max 1s: %u)\n",
        s.rawQueueDepth, s.rawQueueCapacity, s.rawQueueMaxDepth);
    std::printf("  parsed queue  : %u / %u  (max 1s: %u)\n",
        s.parsedQueueDepth, s.parsedQueueCapacity, s.parsedQueueMaxDepth);
    std::printf("  parsed drops  : %llu\n", (unsigned long long)s.parsedEventDrops);
    std::puts("");

    // Parser
    std::printf("Parser:\n");
    std::printf("  parsed/sec    : %llu\n", (unsigned long long)s.parsedPerSec);
    std::printf("  parse errs/sec: %llu\n", (unsigned long long)s.parseErrorsPerSec);
    std::printf("  avg parse lat : %.1f us\n", s.avgParseLatencyUs);
    std::printf("  max parse lat : %.1f us\n", s.maxParseLatencyUs);
    std::puts("");

    // Processing
    std::printf("Processing:\n");
    std::printf("  consumed/sec  : %llu\n", (unsigned long long)s.consumedPerSec);
    std::printf("  avg rq wait   : %.1f us\n", s.avgRawQueueWaitUs);
    std::printf("  avg pq wait   : %.1f us\n", s.avgParsedQueueWaitUs);
    std::printf("  avg e2e local : %.1f us\n", s.avgEndToEndLocalUs);
    std::printf("  max e2e local : %.1f us\n", s.maxEndToEndLocalUs);
    std::printf("  exch lag avg  : %.1f ms\n", s.estimatedExchangeLagMs);
    std::puts("");

    // Per-symbol
    std::printf("Per-Symbol:\n");
    for (const auto& sym : s.symbols) {
        std::printf("  %-10s  trades/s=%-6llu  last=%-12.4f  vol/s=%-10.4f"
                    "  min=%-12.4f  max=%-12.4f  avg=%-12.4f\n",
            sym.name.c_str(),
            (unsigned long long)sym.tradesThisInterval,
            sym.lastPrice,
            sym.volumeThisInterval,
            sym.minPrice,
            sym.maxPrice,
            sym.avgPrice);
    }

    line('-');
    std::fflush(stdout);
}
