#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>

// Fixed-size message holder — preallocated by RawMessagePool.
// MAX_MSG_SIZE is a compile-time constant to keep the struct on the heap
// with a known, fixed footprint.
static constexpr size_t MAX_MSG_SIZE = 8192;

struct RawMessage {
    char     data[MAX_MSG_SIZE];
    size_t   length  = 0;
    uint32_t feedId  = 0;
    std::chrono::steady_clock::time_point receiveTime;

    void reset() {
        length  = 0;
        feedId  = 0;
    }
};
