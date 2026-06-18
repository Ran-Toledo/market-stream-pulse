#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct AppConfig {
    // Symbols to subscribe to
    std::vector<std::string> symbols{
        "btcusdt", "ethusdt", "solusdt", "bnbusdt", "xrpusdt"
    };

    // Buffer pool
    uint32_t rawMessageBufferCount = 131072;
    uint32_t rawMessageMaxSize     = 8192;

    // Queue capacities
    uint32_t rawQueueCapacity    = 65536;
    uint32_t parsedQueueCapacity = 65536;

    // Parser
    uint32_t parserWorkerCount = 2;

    // Reporting
    uint32_t reportIntervalSeconds = 1;

    // Reconnect
    bool reconnectEnabled = true;

    // Binance endpoint
    std::string binanceHost = "stream.binance.com";
    std::string binancePort = "9443";

    // Build the combined stream path from symbols
    std::string buildStreamPath() const {
        std::string path = "/stream?streams=";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) path += '/';
            path += symbols[i] + "@trade";
        }
        return path;
    }
};
