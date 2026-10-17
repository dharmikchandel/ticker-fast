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
//
// The three bool parameters switch the performance changes on and off at compile time,
// so the benchmark can build every combination side by side. Everything else should use
// the defaults (all true).
//   kUseAcquireRelease:     acquire/release instead of seq_cst on the counters.
//   kUseCacheLineAlignment: head_ and tail_ each get their own cache line.
//   kUseCachedIndices:      each side keeps a local copy of the other side's counter.
template <typename T, std::size_t N, bool kUseAcquireRelease = true,
          bool kUseCacheLineAlignment = true, bool kUseCachedIndices = true>
class RingBuffer {
    static_assert(is_power_of_two(N), "RingBuffer capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>,
                  "RingBuffer element must be trivially copyable");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "RingBuffer counters must be lock-free");

  public:
    RingBuffer() noexcept = default;

    // Attempts to push an item into the buffer.
    // Returns false immediately if the buffer is full; never blocks or allocates.
    bool try_push(const T& item) noexcept {
        // relaxed: only the producer writes head_, so it always sees its own latest value.
        const std::uint64_t current_head = head_.load(kOwnOrder);

        if constexpr (kUseCachedIndices) {
            // The cached tail can only be behind the real one, so it can only make the
            // buffer look fuller than it is. Re-read the real tail only when it looks full.
            if (current_head - cached_tail_ == N) {
                // acquire: pairs with the consumer's release store of tail_, so its read
                // of the slot we are about to overwrite has finished.
                cached_tail_ = tail_.load(kAcquireOrder);
                if (current_head - cached_tail_ == N) {
                    return false;
                }
            }
        } else {
            // acquire: pairs with the consumer's release store of tail_ (see above).
            if (current_head - tail_.load(kAcquireOrder) == N) {
                return false;
            }
        }

        buffer_[mask(current_head)] = item;
        // release: the slot write above becomes visible to any thread that
        // acquire-loads this new head_ value.
        head_.store(current_head + 1ULL, kReleaseOrder);
        return true;
    }

    // Attempts to pop an item from the buffer into out.
    // Returns false immediately if the buffer is empty; never blocks or allocates.
    bool try_pop(T& out) noexcept {
        // relaxed: only the consumer writes tail_.
        const std::uint64_t current_tail = tail_.load(kOwnOrder);

        if constexpr (kUseCachedIndices) {
            // The cached head can only be behind the real one, so it can only make the
            // buffer look emptier than it is. Re-read the real head only when it looks empty.
            // Slots below cached_head_ are safe to read: they were covered by the acquire
            // load that produced cached_head_.
            if (cached_head_ == current_tail) {
                // acquire: pairs with the producer's release store of head_, so the slot
                // contents written before that store are visible here.
                cached_head_ = head_.load(kAcquireOrder);
                if (cached_head_ == current_tail) {
                    return false;
                }
            }
        } else {
            // acquire: pairs with the producer's release store of head_ (see above).
            if (head_.load(kAcquireOrder) == current_tail) {
                return false;
            }
        }

        out = buffer_[mask(current_tail)];
        // release: our read of the slot finishes before the producer can see it is free.
        tail_.store(current_tail + 1ULL, kReleaseOrder);
        return true;
    }

    // Returns the approximate number of items currently in the buffer.
    // NOTE: This is only exact when called in a single-threaded context or when
    // producer and consumer are quiescent.
    std::size_t size() const noexcept {
        // acquire on both: a snapshot for reporting only, no slot is read.
        const std::uint64_t current_head = head_.load(kAcquireOrder);
        const std::uint64_t current_tail = tail_.load(kAcquireOrder);
        return static_cast<std::size_t>(current_head - current_tail);
    }

  private:
    // With kUseAcquireRelease off, every operation falls back to seq_cst (the Phase 2 code).
    static constexpr std::memory_order kAcquireOrder =
        kUseAcquireRelease ? std::memory_order_acquire : std::memory_order_seq_cst;
    static constexpr std::memory_order kReleaseOrder =
        kUseAcquireRelease ? std::memory_order_release : std::memory_order_seq_cst;
    static constexpr std::memory_order kOwnOrder =
        kUseAcquireRelease ? std::memory_order_relaxed : std::memory_order_seq_cst;

    // Alignment for each group of fields below. Without the toggle, fields are packed at
    // their natural alignment and head_ and tail_ end up on the same cache line.
    static constexpr std::size_t kFieldAlign =
        kUseCacheLineAlignment ? kCacheLineSize : alignof(std::atomic<std::uint64_t>);

    // Fast bitwise index wrapping: requires N to be a power of two.
    static constexpr std::size_t mask(std::uint64_t index) noexcept {
        return static_cast<std::size_t>(index) & (N - 1);
    }

    // Producer's cache line: the counter it writes, plus its private copy of tail_.
    alignas(kFieldAlign) std::atomic<std::uint64_t> head_{0};
    std::uint64_t cached_tail_{0};

    // Consumer's cache line: the counter it writes, plus its private copy of head_.
    alignas(kFieldAlign) std::atomic<std::uint64_t> tail_{0};
    std::uint64_t cached_head_{0};

    // Preallocated contiguous slot storage. No dynamic allocations on the hot path.
    alignas(kFieldAlign) T buffer_[N]{};
};

}  // namespace ticker_fast
