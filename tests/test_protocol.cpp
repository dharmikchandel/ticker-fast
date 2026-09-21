#include <ticker_fast/protocol.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// 1. Layout: verify sizeof, alignof, and offsetof for every field in MarketTick.
TEST(ProtocolTest, LayoutAndFieldOffsets) {
    // Exactly 64 bytes matches the CPU cache line size.
    EXPECT_EQ(sizeof(ticker_fast::MarketTick), 64U);

    // Cache-line aligned to prevent cache line splitting across two lines.
    EXPECT_EQ(alignof(ticker_fast::MarketTick), ticker_fast::kCacheLineSize);

    // Verify exact byte offsets of all fields within the struct.
    EXPECT_EQ(offsetof(ticker_fast::MarketTick, timestamp_ns), 0U);
    EXPECT_EQ(offsetof(ticker_fast::MarketTick, sequence), 8U);
    EXPECT_EQ(offsetof(ticker_fast::MarketTick, ticker), 16U);
    EXPECT_EQ(offsetof(ticker_fast::MarketTick, bid_price), 24U);
    EXPECT_EQ(offsetof(ticker_fast::MarketTick, ask_price), 32U);
    EXPECT_EQ(offsetof(ticker_fast::MarketTick, volume), 40U);
    EXPECT_EQ(offsetof(ticker_fast::MarketTick, reserved), 44U);
}

// 2. Zero initialization: MarketTick t{} zeroes out all 64 bytes.
TEST(ProtocolTest, ZeroInitialization) {
    const ticker_fast::MarketTick tick{};
    const std::uint8_t zero_buffer[sizeof(ticker_fast::MarketTick)] = {0};

    // Compare all 64 bytes directly against a zeroed buffer.
    EXPECT_EQ(std::memcmp(&tick, zero_buffer, sizeof(tick)), 0);
}

// 3. Copy round trip: verify that memcpy correctly preserves every field.
TEST(ProtocolTest, CopyRoundTrip) {
    ticker_fast::MarketTick src{};
    src.timestamp_ns = 1700000000123456789ULL;
    src.sequence = 42ULL;
    std::memcpy(src.ticker, "AAPL\0\0\0", ticker_fast::kTickerLength);
    src.bid_price = 182.50;
    src.ask_price = 182.55;
    src.volume = 1500U;

    for (std::size_t i = 0; i < sizeof(src.reserved); ++i) {
        src.reserved[i] = static_cast<std::uint8_t>(i + 1);
    }

    ticker_fast::MarketTick dst{};
    std::memcpy(&dst, &src, sizeof(ticker_fast::MarketTick));

    EXPECT_EQ(dst.timestamp_ns, src.timestamp_ns);
    EXPECT_EQ(dst.sequence, src.sequence);
    EXPECT_EQ(std::memcmp(dst.ticker, src.ticker, ticker_fast::kTickerLength), 0);
    EXPECT_DOUBLE_EQ(dst.bid_price, src.bid_price);
    EXPECT_DOUBLE_EQ(dst.ask_price, src.ask_price);
    EXPECT_EQ(dst.volume, src.volume);
    EXPECT_EQ(std::memcmp(dst.reserved, src.reserved, sizeof(src.reserved)), 0);
}

// 4. is_power_of_two: verify behavior on known power-of-two and non-power-of-two inputs.
TEST(ProtocolTest, IsPowerOfTwo) {
    EXPECT_TRUE(ticker_fast::is_power_of_two(1));
    EXPECT_TRUE(ticker_fast::is_power_of_two(2));
    EXPECT_TRUE(ticker_fast::is_power_of_two(4));
    EXPECT_TRUE(ticker_fast::is_power_of_two(1024));
    EXPECT_TRUE(ticker_fast::is_power_of_two(4096));

    EXPECT_FALSE(ticker_fast::is_power_of_two(0));
    EXPECT_FALSE(ticker_fast::is_power_of_two(3));
    EXPECT_FALSE(ticker_fast::is_power_of_two(6));
    EXPECT_FALSE(ticker_fast::is_power_of_two(1000));
}

// 5. Why we mask: bitwise masking (i & (N - 1)) is equivalent to modulo (i % N)
// for any power of two, but executes in ~1 CPU cycle instead of ~10-40 cycles for
// hardware division (idiv). Furthermore, when a uint64_t counter overflows past
// UINT64_MAX, (i & (N - 1)) wraps around seamlessly without discontinuous jumps.
TEST(ProtocolTest, WhyWeMask) {
    constexpr std::uint64_t kCap = ticker_fast::kRingCapacity;
    constexpr std::uint64_t kMask = kCap - 1ULL;

    const std::vector<std::uint64_t> test_indices = {
        0ULL,
        1ULL,
        kCap - 1ULL,
        kCap,
        kCap + 1ULL,
        UINT64_MAX - 100ULL,
        UINT64_MAX - 1ULL,
        UINT64_MAX,
    };

    for (const std::uint64_t idx : test_indices) {
        const std::uint64_t masked = idx & kMask;
        const std::uint64_t modulo = idx % kCap;
        EXPECT_EQ(masked, modulo) << "Failed for index: " << idx;
    }
}

}  // namespace
