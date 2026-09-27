#include <ticker_fast/protocol.hpp>
#include <ticker_fast/ring_buffer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {

// Milestone 1 Stress Test: Two-thread correctness and throughput.
// Verifies lock-free Single Producer, Single Consumer (SPSC) execution with
// a producer thread continuously pushing market ticks and a consumer thread
// verifying monotonic sequence numbers and checksum integrity across 10 million ticks.
TEST(RingBufferStressTest, SpscTenMillionTicks) {
    // Total number of ticks to stream across the two threads.
    // Named constant allows easy adjustment for quick debugging or sanitizer runs.
    constexpr std::uint64_t kTickCount = 10'000'000ULL;

    // Use the production cache-line-aligned RingBuffer with capacity 4096.
    ticker_fast::RingBuffer<ticker_fast::MarketTick, ticker_fast::kRingCapacity> ring_buffer;

    // Concurrency state & abort flag to avoid thread deadlocks upon assertion failures.
    std::atomic<bool> stop_requested{false};

    // Shared metrics captured by threads
    std::uint64_t received_count = 0;
    std::uint64_t sequence_mismatch_count = 0;
    std::uint64_t first_mismatch_expected = 0;
    std::uint64_t first_mismatch_actual = 0;

    double expected_bid_sum = 0.0;
    double actual_bid_sum = 0.0;

    const auto start_time = std::chrono::steady_clock::now();

    // Producer thread: streams kTickCount ticks as fast as the ring buffer permits.
    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < kTickCount; ++i) {
            ticker_fast::MarketTick tick{};
            tick.sequence = i;
            tick.timestamp_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            std::memcpy(tick.ticker, "AAPL\0\0\0", ticker_fast::kTickerLength);
            tick.bid_price = 150.0 + static_cast<double>(i % 50ULL);
            tick.ask_price = tick.bid_price + 0.05;
            tick.volume = 100U + static_cast<std::uint32_t>(i % 10ULL);

            expected_bid_sum += tick.bid_price;

            // Spin-push until a slot becomes available.
            while (!ring_buffer.try_push(tick)) {
                if (stop_requested.load()) {
                    return;
                }
            }
        }
    });

    // Consumer thread: pops ticks and verifies strict sequence order and payload integrity.
    std::thread consumer([&]() {
        std::uint64_t expected_seq = 0;
        ticker_fast::MarketTick tick{};

        while (received_count < kTickCount) {
            if (ring_buffer.try_pop(tick)) {
                if (tick.sequence != expected_seq) {
                    sequence_mismatch_count++;
                    first_mismatch_expected = expected_seq;
                    first_mismatch_actual = tick.sequence;
                    stop_requested.store(true);
                    break;
                }

                actual_bid_sum += tick.bid_price;
                ++expected_seq;
                ++received_count;
            } else if (stop_requested.load()) {
                break;
            }
        }
    });

    // Wait for both worker threads to conclude.
    producer.join();
    consumer.join();

    const auto end_time = std::chrono::steady_clock::now();
    const double duration_seconds =
        std::chrono::duration<double>(end_time - start_time).count();
    const double throughput = static_cast<double>(received_count) / duration_seconds;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[          ] Stress test throughput: " << throughput << " ticks/sec ("
              << duration_seconds << "s for " << received_count << " ticks)\n";

    // Validate Milestone 1 correctness guarantees.
    EXPECT_EQ(sequence_mismatch_count, 0ULL)
        << "Sequence gap detected: expected " << first_mismatch_expected << ", got "
        << first_mismatch_actual;
    EXPECT_EQ(received_count, kTickCount);
    EXPECT_DOUBLE_EQ(actual_bid_sum, expected_bid_sum);
}

}  // namespace
