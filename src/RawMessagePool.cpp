#include "RawMessagePool.h"
#include <stdexcept>

RawMessagePool::RawMessagePool(uint32_t capacity)
    : capacity_(capacity)
{
    if (capacity == 0) throw std::invalid_argument("Pool capacity must be > 0");
    nodes_ = std::make_unique<Node[]>(capacity);  // single allocation at startup

    // Push all nodes onto the free stack
    for (uint32_t i = 0; i < capacity; ++i) {
        Node* n = &nodes_[i];
        n->next.store(head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        head_.store(n, std::memory_order_relaxed);
    }
}

RawMessage* RawMessagePool::acquire() {
    Node* top = head_.load(std::memory_order_acquire);
    while (top) {
        Node* next = top->next.load(std::memory_order_relaxed);
        if (head_.compare_exchange_weak(top, next,
                                        std::memory_order_release,
                                        std::memory_order_acquire)) {
            top->msg.reset();
            return &top->msg;
        }
    }
    exhausted_.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

void RawMessagePool::release(RawMessage* msg) {
    // Recover the Node* from the RawMessage* (msg is the first member of Node)
    Node* n = reinterpret_cast<Node*>(msg);
    Node* top = head_.load(std::memory_order_relaxed);
    do {
        n->next.store(top, std::memory_order_relaxed);
    } while (!head_.compare_exchange_weak(top, n,
                                          std::memory_order_release,
                                          std::memory_order_acquire));
}
