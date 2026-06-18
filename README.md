# Market Stream Pulse

A real-time market-data ingestion and processing metrics application written in C++17.

Connects to live **Binance public WebSocket trade streams** for multiple symbols, processes messages through a bounded-queue pipeline with parser worker threads, and prints detailed throughput and latency metrics every second.

---

## Architecture

```
 ┌──────────────────────────────────────────────────────────────────────┐
 │                        BinanceWebSocketClient                        │
 │  (Boost.Beast / TLS / dedicated io_context thread)                   │
 │                                                                      │
 │  Receive frame  →  acquire RawMessage from pool                      │
 │                 →  memcpy payload                                    │
 │                 →  stamp receiveTime                                 │
 │                 →  push* to raw queue                               │
 │                    * drops frame if pool exhausted or queue full     │
 └───────────────────────────┬──────────────────────────────────────────┘
                             │  BoundedBlockingQueue<RawMessage*>
                             │  (fixed capacity, mutex + condvar)
              ┌──────────────┴──────────────┐
              │       Parser Workers (N)    │
              │  pop RawMessage*            │
              │  BinanceTradeParser::parse()│  ← nlohmann/json (may alloc)
              │  stamp parsedTime           │
              │  release buffer to pool     │
              │  push TradeEvent to queue   │
              └──────────────┬──────────────┘
                             │  BoundedBlockingQueue<TradeEvent>
                             │  (fixed capacity, mutex + condvar)
              ┌──────────────┴──────────────┐
              │       TradeAggregator       │
              │  (single thread owns state) │
              │  update per-symbol stats    │
              │  measure latencies          │
              │  publish MetricsSnapshot    │
              └──────────────┬──────────────┘
                             │  mutex-protected snapshot
              ┌──────────────┴──────────────┐
              │       ConsoleReporter       │
              │  (main thread, 1/sec)       │
              │  read snapshot              │
              │  print report               │
              └─────────────────────────────┘
```

### Key design properties

| Property | Implementation |
|---|---|
| **Bounded queues** | Fixed-capacity ring buffers; never resize at runtime |
| **Preallocated buffer pool** | Lock-free Treiber stack of `RawMessage` objects |
| **No app-level dynamic allocation at runtime** | All pools/queues/per-symbol state preallocated at startup |
| **Parser isolation** | Workers don't touch aggregation state; aggregator owns its state lock-free |
| **Multiple symbols** | `SymbolRegistry` maps names → integer IDs at startup; hot path uses IDs only |
| **Multi-feed ready** | `feedId` field in `RawMessage` and `TradeEvent`; second adapter just needs its own queue push |

### Known MVP limitations

1. **Symbol lookup stack copy** — `symbolIdCI()` lowercases the symbol into a 33-byte stack buffer before the hash lookup. No heap allocation, but a small per-message copy. **TODO**: store symbols pre-lowercased so the copy is eliminated entirely.
2. **Mutex-based queues** — `BoundedBlockingQueue` uses `std::mutex` + `std::condition_variable`. **TODO**: replace with a lock-free MPMC ring buffer.
3. **Single reconnect** — reconnect has a 3-second hardcoded delay and no exponential backoff. **TODO**: add jittered exponential backoff.
4. **No p50/p95/p99** — latency metrics use a simple count/sum/max accumulator. **TODO**: integrate HDR histogram.
5. **Wall-clock exchange lag** — the estimated exchange lag assumes the local clock is reasonably synced (within ~100 ms). No NTP validation is performed.

---

## Prerequisites

| Dependency | Version |
|---|---|
| C++ compiler | MSVC 2019+, GCC 11+, or Clang 14+ |
| CMake | 3.16+ |
| vcpkg | bundled as a git submodule — no separate install needed |
| OpenSSL | fetched and built by vcpkg |
| Boost.Asio / Beast | fetched and built by vcpkg |
| simdjson | fetched and built by vcpkg |

---

## Build (Windows)

```powershell
# 1. Clone with submodules (vcpkg is bundled)
git clone --recurse-submodules https://github.com/your-org/market-stream-pulse.git
cd market-stream-pulse

# 2. Bootstrap vcpkg
.\vcpkg\bootstrap-vcpkg.bat

# 3. Configure (dependencies from vcpkg.json install automatically)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 4. Build
cmake --build build --config Release

# 5. Run
.\build\Release\market_stream_pulse.exe
```

## Build (Linux / macOS)

```bash
git clone --recurse-submodules https://github.com/your-org/market-stream-pulse.git
cd market-stream-pulse
./vcpkg/bootstrap-vcpkg.sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/market_stream_pulse
```

---

## Sample output

```
=== Market Stream Pulse ===
Symbols: btcusdt ethusdt solusdt bnbusdt xrpusdt
Buffer pool: 131072 x 8192 bytes
Raw queue: 65536  Parsed queue: 65536  Workers: 2

[WS] Connected to stream.binance.com/stream?streams=btcusdt@trade/...

============================================================
  MarketStream Pulse  |  Metrics Report
============================================================
Connection:
  status        : connected
  feeds         : 1
  symbols       : 5
  parser workers: 2

Input:
  raw msgs/sec  : 8412
  raw bytes/sec : 5234880
  total raw msgs: 8412
  oversized     : 0
  dropped/no buf: 0
  dropped/rq full:0

Queues:
  raw queue     : 142 / 65536  (max 1s: 610)
  parsed queue  : 88 / 65536  (max 1s: 492)
  parsed drops  : 0

Parser:
  parsed/sec    : 8410
  parse errs/sec: 0
  avg parse lat : 22.4 us
  max parse lat : 318.0 us

Processing:
  consumed/sec  : 8408
  avg rq wait   : 180.3 us
  avg pq wait   : 95.1 us
  avg e2e local : 302.8 us
  max e2e local : 1940.0 us
  exch lag avg  : 87.2 ms

Per-Symbol:
  BTCUSDT     trades/s=3810   last=104321.2500  vol/s=397220.1000  min=104100.1000  max=104400.2500  avg=104230.7700
  ETHUSDT     trades/s=2100   last=5220.1000    vol/s=11004.2100   min=5199.0000    max=5230.5000    avg=5210.7000
  SOLUSDT     trades/s=1200   last=188.4200     vol/s=226.1040     min=187.9000     max=188.8000     avg=188.3500
  BNBUSDT     trades/s=712    last=621.3000     vol/s=442.0000     min=619.0000     max=623.0000     avg=621.0000
  XRPUSDT     trades/s=590    last=0.5420       vol/s=0.3198       min=0.5400       max=0.5440       avg=0.5420
------------------------------------------------------------
```

---

## Optimization roadmap (TODOs)

1. **Eliminate symbol lowercase copy** — store symbols pre-lowercased in `SymbolRegistry` so `symbolIdCI` needs no stack buffer transformation.
2. **Lock-free queues** — replace `BoundedBlockingQueue` with a MPMC ring buffer (e.g. `moodycamel::ConcurrentQueue` or a custom power-of-2 ring buffer with atomic head/tail).
3. **Per-thread metric counters** — reduce atomic contention by accumulating metrics thread-locally and flushing to shared counters once per interval.
4. **Latency histograms** — add p50/p95/p99 using HDR Histogram (`hdrhistogram_c` or a C++ port).
5. **Scaled integer prices** — store price/quantity as `int64_t` (fixed-point, e.g. 8 decimal places) to eliminate floating-point in the hot path.
6. **CPU affinity + thread naming** — pin io thread, parser threads, and aggregator to specific cores; name threads for profiler visibility.
7. **Second feed adapter** — add Coinbase Advanced Trade WebSocket adapter behind the same queue interface.
8. **Order-book depth** — subscribe to `btcusdt@depth@100ms` and maintain a local L2 order book.
9. **Prometheus endpoint** — expose `/metrics` via a lightweight HTTP server (Crow or Beast HTTP) for Grafana dashboards.
10. **CSV / SQLite persistence** — sample 1-second snapshots to disk for post-run analysis and replay.
11. **Replay / benchmark mode** — replay captured WebSocket frames at configurable speed for deterministic benchmarking.
12. **Memory allocation instrumentation** — intercept `operator new` in debug builds to verify zero hot-path allocations.
13. **Simdjson SIMD field extractor** — hand-write a `btcusdt@trade` parser that extracts only the 6 needed fields using SIMD string scanning.
