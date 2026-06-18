#pragma once
#include "RawMessage.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Lock-free Treiber-stack pool for RawMessage objects.
// All RawMessage storage is preallocated at construction — no allocation
// occurs during acquire() / release().
class RawMessagePool {
public:
    explicit RawMessagePool(uint32_t capacity);
    ~RawMessagePool() = default;

    // Returns nullptr if pool exhausted.
    RawMessage* acquire();

    // Returns msg to the pool.  msg must have been obtained from this pool.
    void release(RawMessage* msg);

    uint64_t exhaustionCount() const { return exhausted_.load(std::memory_order_relaxed); }
    uint32_t capacity()        const { return capacity_; }

private:
    struct Node {
        RawMessage  msg;
        std::atomic<Node*> next{nullptr};
    };

    uint32_t capacity_;
    // Backing store — allocated once at startup.
    std::vector<Node> nodes_;
    // Lock-free stack head.
    std::atomic<Node*> head_{nullptr};

    std::atomic<uint64_t> exhausted_{0};
};
