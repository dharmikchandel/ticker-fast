// Throughput benchmark for the three RingBuffer toggles (memory order, cache-line
// alignment, cached indices). Builds all 8 combinations into one binary and runs each
// one with a producer and a consumer thread pinned to different physical cores.
//
// Usage: mechanical_sympathy_bench [producer_cpu] [consumer_cpu]   (defaults: 0 and 2)

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

#include "ticker_fast/protocol.hpp"
#include "ticker_fast/ring_buffer.hpp"

namespace {

constexpr std::uint64_t kTickCount = 50'000'000ULL;
constexpr int kRuns = 5;

// Pins the calling thread to one CPU. Returns false if the kernel refused.
bool pin_current_thread(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

// Runs one producer/consumer pass and returns ticks per second.
// The consumer checks every sequence number so a broken variant cannot report a number.
template <bool kAcqRel, bool kAlign, bool kCached>
double run_once(int producer_cpu, int consumer_cpu) {
    using Ring = ticker_fast::RingBuffer<ticker_fast::MarketTick, ticker_fast::kRingCapacity,
                                         kAcqRel, kAlign, kCached>;
    // Setup, not hot path: a fresh ring per run so no run inherits a warm buffer.
    auto ring = std::make_unique<Ring>();

    std::atomic<bool> go{false};
    std::uint64_t mismatches = 0;

    std::thread producer([&] {
        if (!pin_current_thread(producer_cpu)) {
            std::fprintf(stderr, "warning: could not pin producer to cpu %d\n", producer_cpu);
        }
        ticker_fast::MarketTick tick{};
        std::memcpy(tick.ticker, "AAPL\0\0\0", ticker_fast::kTickerLength);
        tick.bid_price = 150.0;
        tick.ask_price = 150.05;
        tick.volume = 100U;
        while (!go.load(std::memory_order_acquire)) {
        }
        for (std::uint64_t i = 0; i < kTickCount; ++i) {
            // No clock call per tick: it would cost more than the push we are measuring.
            tick.sequence = i;
            while (!ring->try_push(tick)) {
            }
        }
    });

    std::thread consumer([&] {
        if (!pin_current_thread(consumer_cpu)) {
            std::fprintf(stderr, "warning: could not pin consumer to cpu %d\n", consumer_cpu);
        }
        ticker_fast::MarketTick tick{};
        while (!go.load(std::memory_order_acquire)) {
        }
        for (std::uint64_t expected = 0; expected < kTickCount; ++expected) {
            while (!ring->try_pop(tick)) {
            }
            if (tick.sequence != expected) {
                ++mismatches;
            }
        }
    });

    // Give both threads time to pin themselves before the clock starts.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto start = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    const auto end = std::chrono::steady_clock::now();

    if (mismatches != 0) {
        std::fprintf(stderr, "error: %llu sequence mismatches\n",
                     static_cast<unsigned long long>(mismatches));
        std::exit(1);
    }
    const double seconds = std::chrono::duration<double>(end - start).count();
    return static_cast<double>(kTickCount) / seconds;
}

struct Variant {
    bool acq_rel;
    bool align;
    bool cached;
    double (*run)(int, int);
};

// All 8 combinations, from the Phase 3 baseline (all off) to the final version (all on).
constexpr std::array<Variant, 8> kVariants{{
    {false, false, false, &run_once<false, false, false>},
    {true, false, false, &run_once<true, false, false>},
    {false, true, false, &run_once<false, true, false>},
    {false, false, true, &run_once<false, false, true>},
    {true, true, false, &run_once<true, true, false>},
    {true, false, true, &run_once<true, false, true>},
    {false, true, true, &run_once<false, true, true>},
    {true, true, true, &run_once<true, true, true>},
}};

const char* on_off(bool value) { return value ? "on" : "off"; }

}  // namespace

int main(int argc, char** argv) {
    const int producer_cpu = argc > 1 ? std::atoi(argv[1]) : 0;
    const int consumer_cpu = argc > 2 ? std::atoi(argv[2]) : 2;
    std::printf("ticks per run: %llu, runs: %d, producer cpu: %d, consumer cpu: %d\n",
                static_cast<unsigned long long>(kTickCount), kRuns, producer_cpu, consumer_cpu);

    // Warm-up pass (CPU frequency, page faults on the ring); results are discarded.
    for (const Variant& v : kVariants) {
        v.run(producer_cpu, consumer_cpu);
    }

    // Runs are interleaved across variants so slow drift (thermals, background work)
    // spreads over all of them instead of hitting whichever ran last.
    std::array<std::array<double, kRuns>, kVariants.size()> results{};
    for (int run = 0; run < kRuns; ++run) {
        for (std::size_t i = 0; i < kVariants.size(); ++i) {
            const Variant& v = kVariants[i];
            results[i][static_cast<std::size_t>(run)] = v.run(producer_cpu, consumer_cpu);
            std::printf("run %d  acq_rel=%-3s align=%-3s cached=%-3s  %8.2f M ticks/s\n", run + 1,
                        on_off(v.acq_rel), on_off(v.align), on_off(v.cached),
                        results[i][static_cast<std::size_t>(run)] / 1e6);
        }
    }

    std::printf("\n| acq/rel | aligned | cached | mean (M/s) | best (M/s) | worst (M/s) |\n");
    std::printf("|---|---|---|---|---|---|\n");
    for (std::size_t i = 0; i < kVariants.size(); ++i) {
        const auto& r = results[i];
        double sum = 0.0;
        for (double x : r) {
            sum += x;
        }
        std::printf("| %s | %s | %s | %.2f | %.2f | %.2f |\n", on_off(kVariants[i].acq_rel),
                    on_off(kVariants[i].align), on_off(kVariants[i].cached),
                    sum / kRuns / 1e6, *std::max_element(r.begin(), r.end()) / 1e6,
                    *std::min_element(r.begin(), r.end()) / 1e6);
    }
    return 0;
}
