#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ticker_fast {

// Cache line size on standard modern x86-64 / ARM64 architectures (64 bytes).
inline constexpr std::size_t kCacheLineSize = 64;

// Fixed length for the stock ticker symbol (e.g., "AAPL\0\0\0\0").
inline constexpr std::size_t kTickerLength = 8;

// Capacity of the ring buffer, must be a power of two to allow bitwise masking.
inline constexpr std::size_t kRingCapacity = 4096;  // MUST be a power of two

// Returns true if n is a non-zero power of two.
// Bit trick: subtracting 1 from a power of two flips all lower bits (e.g. 8: 1000, 7: 0111).
// ANDing them produces 0. For any other number, at least one common bit remains.
constexpr bool is_power_of_two(std::size_t n) noexcept {
    return n != 0 && (n & (n - 1)) == 0;
}

// Compile-time check: ensures the ring capacity is a power of two before compilation can succeed.
static_assert(is_power_of_two(kRingCapacity), "kRingCapacity must be a power of two");

// One market data update. It will live inside shared memory, so it must be plain data:
// fixed-width types, fixed-size arrays, no pointers, no std::string, no constructors,
// no default member initializers. Keep it an aggregate.
struct alignas(kCacheLineSize) MarketTick {
    std::uint64_t timestamp_ns;   // publisher clock when the tick was created
    std::uint64_t sequence;       // 0, 1, 2, ... assigned by the publisher, used to detect gaps
    char ticker[kTickerLength];   // e.g. "AAPL\0\0\0\0"
    double bid_price;
    double ask_price;
    std::uint32_t volume;
    std::uint8_t reserved[20];    // explicit padding so the struct is exactly 64 bytes
};

// Compile-time layout guarantees:
// 1. Exactly 64 bytes matches the hardware cache line, preventing false sharing across ticks.
static_assert(sizeof(MarketTick) == 64, "MarketTick must be exactly one cache line");

// 2. Cache-line alignment ensures each tick starts at a 64-byte boundary in memory.
static_assert(alignof(MarketTick) == kCacheLineSize, "MarketTick must be cache-line aligned");

// 3. Trivially copyable means the struct can be safely copied using std::memcpy / byte copy,
//    which is required when moving data across shared memory boundaries.
static_assert(std::is_trivially_copyable_v<MarketTick>, "MarketTick must be safe to memcpy");

// 4. Standard layout guarantees a C-compatible memory layout with no hidden compiler metadata
//    like vtables, predictable across different processes or compilers.
static_assert(std::is_standard_layout_v<MarketTick>, "MarketTick must have a predictable layout");

}  // namespace ticker_fast
