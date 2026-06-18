#pragma once
#include "Metrics.h"
#include "TradeAggregator.h"

class ConsoleReporter {
public:
    explicit ConsoleReporter(const TradeAggregator& aggregator);

    // Print the latest snapshot to stdout.
    void report();

private:
    const TradeAggregator& aggregator_;
};
