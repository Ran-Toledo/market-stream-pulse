#pragma once
#include "AppConfig.h"
#include "BoundedBlockingQueue.h"
#include "Metrics.h"
#include "RawMessage.h"
#include "RawMessagePool.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>

// Connects to the Binance combined WebSocket stream and feeds raw frames into
// the bounded raw-message queue.  Runs its own io_context on a dedicated thread.
// Does NOT parse JSON — the receive callback is intentionally thin.
class BinanceWebSocketClient {
public:
    BinanceWebSocketClient(const AppConfig& cfg,
                           RawMessagePool& pool,
                           BoundedBlockingQueue<RawMessage*>& rawQueue,
                           PipelineMetrics& metrics);
    ~BinanceWebSocketClient();

    void start();
    void stop();

    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }

private:
    void run();         // io_context thread entry point
    void connect();
    void doRead();
    void handleFrame(const char* data, size_t len);
    void scheduleReconnect();

    const AppConfig& cfg_;
    RawMessagePool& pool_;
    BoundedBlockingQueue<RawMessage*>& rawQueue_;
    PipelineMetrics& metrics_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread thread_;

    // Beast/Asio objects are heap-allocated so the header doesn't pull
    // in all of Boost.Beast (keeps compilation fast and avoids template bloat).
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
