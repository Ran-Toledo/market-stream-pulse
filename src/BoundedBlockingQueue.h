#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

// Fixed-capacity, mutex-based blocking queue.
// push() is non-blocking — returns false immediately if full or closed.
// pop()  blocks until an item is available or the queue is closed.
//
// TODO: replace with a lock-free MPMC ring buffer (e.g. moodycamel or custom)
//       to eliminate mutex overhead on the hot path.
template<typename T>
class BoundedBlockingQueue {
public:
    explicit BoundedBlockingQueue(uint32_t capacity)
        : capacity_(capacity)
        , ring_(capacity)
        , head_(0)
        , tail_(0)
        , size_(0)
        , maxDepthInterval_(0)
        , enqueueAttempts_(0)
        , enqueueFailures_(0)
        , closed_(false)
    {}

    // Returns true on success, false if full or closed.
    bool push(T item) {
        enqueueAttempts_.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_ || size_ == capacity_) {
            enqueueFailures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        ring_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;
        uint32_t d = size_;
        lock.unlock();
        cond_.notify_one();

        // Track max depth without holding the lock
        uint32_t prev = maxDepthInterval_.load(std::memory_order_relaxed);
        while (prev < d &&
               !maxDepthInterval_.compare_exchange_weak(prev, d,
                                                        std::memory_order_relaxed))
        {}
        return true;
    }

    // Blocks until item available or closed.  Returns false if closed and empty.
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]{ return size_ > 0 || closed_; });
        if (size_ == 0) return false;
        item = std::move(ring_[head_]);
        head_ = (head_ + 1) % capacity_;
        --size_;
        return true;
    }

    // Blocks up to `timeout` for an item.  Returns false on timeout or closed+empty.
    template<typename Rep, typename Period>
    bool pop_for(T& item, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ready = cond_.wait_for(lock, timeout,
                                    [this]{ return size_ > 0 || closed_; });
        if (!ready || size_ == 0) return false;
        item = std::move(ring_[head_]);
        head_ = (head_ + 1) % capacity_;
        --size_;
        return true;
    }

    // Signal all waiters to exit.
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cond_.notify_all();
    }

    bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    uint32_t depth() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    uint32_t capacity() const { return capacity_; }

    // Atomically read and reset the interval max depth.
    uint32_t swapMaxDepthInterval() {
        return maxDepthInterval_.exchange(0, std::memory_order_relaxed);
    }

    uint64_t enqueueAttempts() const { return enqueueAttempts_.load(std::memory_order_relaxed); }
    uint64_t enqueueFailures() const { return enqueueFailures_.load(std::memory_order_relaxed); }

private:
    uint32_t capacity_;
    std::vector<T> ring_;
    uint32_t head_;
    uint32_t tail_;
    uint32_t size_;

    mutable std::mutex mutex_;
    std::condition_variable cond_;

    std::atomic<uint32_t> maxDepthInterval_;
    std::atomic<uint64_t> enqueueAttempts_;
    std::atomic<uint64_t> enqueueFailures_;
    bool closed_;
};
