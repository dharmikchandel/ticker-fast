#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "ticker_fast/protocol.hpp"

namespace ticker_fast {

// Single-producer, single-consumer lock-free ring buffer.
// One producer thread may call try_push, and one consumer thread may call try_pop
// concurrently without any mutexes or kernel locks.
// Calling try_push concurrently from multiple producers, or try_pop from multiple
// consumers, is NOT thread-safe.
//
// head_ tracks the cumulative count of pushed items.
// tail_ tracks the cumulative count of popped items.
// Neither counter is ever masked when stored; only the array index is masked via
// `index & (N - 1)`. This guarantees exact distance calculations even across 64-bit
// integer overflow boundaries.
template <typename T, std::size_t N>
class RingBuffer {
    static_assert(is_power_of_two(N), "RingBuffer capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>,
                  "RingBuffer element must be trivially copyable");

  public:
    RingBuffer() noexcept = default;

    // Attempts to push an item into the buffer.
    // Returns false immediately if the buffer is full; never blocks or allocates.
    bool try_push(const T& item) noexcept {
        const std::uint64_t current_head = head_.load();
        const std::uint64_t current_tail = tail_.load();

        // Buffer is full when the distance between head and tail reaches capacity N.
        if (current_head - current_tail == N) {
            return false;
        }

        buffer_[mask(current_head)] = item;
        head_.store(current_head + 1ULL);
        return true;
    }

    // Attempts to pop an item from the buffer into out.
    // Returns false immediately if the buffer is empty; never blocks or allocates.
    bool try_pop(T& out) noexcept {
        const std::uint64_t current_tail = tail_.load();
        const std::uint64_t current_head = head_.load();

        // Buffer is empty when head equals tail.
        if (current_head == current_tail) {
            return false;
        }

        out = buffer_[mask(current_tail)];
        tail_.store(current_tail + 1ULL);
        return true;
    }

    // Returns the approximate number of items currently in the buffer.
    // NOTE: This is only exact when called in a single-threaded context or when
    // producer and consumer are quiescent.
    std::size_t size() const noexcept {
        const std::uint64_t current_head = head_.load();
        const std::uint64_t current_tail = tail_.load();
        return static_cast<std::size_t>(current_head - current_tail);
    }

  private:
    // Fast bitwise index wrapping: requires N to be a power of two.
    static constexpr std::size_t mask(std::uint64_t index) noexcept {
        return static_cast<std::size_t>(index) & (N - 1);
    }

    // Cumulative write sequence counter (default sequentially consistent ordering).
    std::atomic<std::uint64_t> head_{0};

    // Cumulative read sequence counter (default sequentially consistent ordering).
    std::atomic<std::uint64_t> tail_{0};

    // Preallocated contiguous slot storage. No dynamic allocations on the hot path.
    T buffer_[N]{};
};

}  // namespace ticker_fast
