#include "BinanceWebSocketClient.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
namespace ws    = beast::websocket;
namespace ssl   = asio::ssl;
using tcp       = asio::ip::tcp;

// ---------------------------------------------------------------------------
// Pimpl — hides all Boost.Beast types from the header
// ---------------------------------------------------------------------------
struct BinanceWebSocketClient::Impl {
    asio::io_context                               ioc;
    ssl::context                                   sslCtx{ssl::context::tlsv12_client};
    std::unique_ptr<ws::stream<ssl::stream<tcp::socket>>> wss;
    beast::flat_buffer                             readBuf;
};

// ---------------------------------------------------------------------------
BinanceWebSocketClient::BinanceWebSocketClient(
    const AppConfig& cfg,
    RawMessagePool& pool,
    BoundedBlockingQueue<RawMessage*>& rawQueue,
    PipelineMetrics& metrics)
    : cfg_(cfg)
    , pool_(pool)
    , rawQueue_(rawQueue)
    , metrics_(metrics)
    , impl_(std::make_unique<Impl>())
{
    // Accept server certificates using the default platform cert store.
    impl_->sslCtx.set_default_verify_paths();
    impl_->sslCtx.set_verify_mode(ssl::verify_peer);
}

BinanceWebSocketClient::~BinanceWebSocketClient() {
    stop();
}

void BinanceWebSocketClient::start() {
    running_.store(true);
    thread_ = std::thread(&BinanceWebSocketClient::run, this);
}

void BinanceWebSocketClient::stop() {
    running_.store(false);
    // Best-effort: try to close the websocket so the synchronous read unblocks.
    // If already disconnected this is a no-op.
    if (impl_->wss) {
        beast::error_code ec;
        impl_->wss->close(ws::close_code::normal, ec);
    }
    if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------
void BinanceWebSocketClient::run() {
    while (running_.load()) {
        try {
            connect();
            doRead();           // blocks until stream ends or error
        } catch (const std::exception& e) {
            connected_.store(false);
            metrics_.connected.store(false);
            std::cerr << "[WS] Error: " << e.what() << "\n";
        }

        if (!running_.load()) break;
        if (!cfg_.reconnectEnabled) break;

        std::cerr << "[WS] Reconnecting in 3 seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // Reset the io_context and rebuild the stream for a clean reconnect.
        impl_->ioc.restart();
        impl_->wss.reset();
        impl_->readBuf.clear();
    }
}

void BinanceWebSocketClient::connect() {
    auto& ioc    = impl_->ioc;
    auto& sslCtx = impl_->sslCtx;

    tcp::resolver resolver{ioc};
    auto results = resolver.resolve(cfg_.binanceHost, cfg_.binancePort);

    auto sock = std::make_unique<ws::stream<ssl::stream<tcp::socket>>>(ioc, sslCtx);

    // TCP connect
    auto ep = asio::connect(beast::get_lowest_layer(*sock), results);
    (void)ep;

    // SNI
    if (!SSL_set_tlsext_host_name(sock->next_layer().native_handle(),
                                   cfg_.binanceHost.c_str())) {
        throw std::runtime_error("SSL_set_tlsext_host_name failed");
    }

    // TLS handshake
    sock->next_layer().handshake(ssl::stream_base::client);

    // WebSocket handshake
    std::string hostHeader = cfg_.binanceHost + ":" + cfg_.binancePort;
    sock->set_option(ws::stream_base::decorator([](ws::request_type& req) {
        req.set(http::field::user_agent, "MarketStreamPulse/1.0");
    }));
    sock->handshake(hostHeader, cfg_.buildStreamPath());

    impl_->wss = std::move(sock);
    connected_.store(true);
    metrics_.connected.store(true);
    std::cerr << "[WS] Connected to " << cfg_.binanceHost
              << cfg_.buildStreamPath() << "\n";
}

void BinanceWebSocketClient::doRead() {
    auto& wss = *impl_->wss;
    auto& buf = impl_->readBuf;

    while (running_.load()) {
        buf.clear();
        beast::error_code ec;
        wss.read(buf, ec);

        if (ec) {
            connected_.store(false);
            metrics_.connected.store(false);
            if (ec == ws::error::closed ||
                ec == asio::error::eof ||
                ec == asio::error::connection_reset) {
                std::cerr << "[WS] Connection closed: " << ec.message() << "\n";
            } else {
                std::cerr << "[WS] Read error: " << ec.message() << "\n";
            }
            return;
        }

        if (!wss.got_text()) continue;  // ignore binary frames

        const char* data = static_cast<const char*>(buf.data().data());
        size_t      len  = buf.size();
        handleFrame(data, len);
    }
}

void BinanceWebSocketClient::handleFrame(const char* data, size_t len) {
    metrics_.totalRawMessages.fetch_add(1, std::memory_order_relaxed);
    metrics_.totalRawBytes.fetch_add(len, std::memory_order_relaxed);

    if (len > MAX_MSG_SIZE) {
        metrics_.oversizedMessages.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    RawMessage* msg = pool_.acquire();
    if (!msg) {
        metrics_.droppedNoBuffer.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::memcpy(msg->data, data, len);
    msg->length      = len;
    msg->feedId      = 0;
    msg->receiveTime = std::chrono::steady_clock::now();

    if (!rawQueue_.push(msg)) {
        metrics_.droppedRawQueueFull.fetch_add(1, std::memory_order_relaxed);
        pool_.release(msg);
    }
}
