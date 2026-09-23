#include <ticker_fast/ring_buffer.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Lightweight trivially copyable payload for ring buffer testing.
struct TestItem {
    int value{0};
};

// 1. Empty pop fails: a freshly constructed buffer's try_pop returns false.
TEST(RingBufferTest, EmptyPopFails) {
    ticker_fast::RingBuffer<TestItem, 4> rb;
    TestItem out{99};

    EXPECT_EQ(rb.size(), 0U);
    EXPECT_FALSE(rb.try_pop(out));
    // Verify out was untouched when pop failed
    EXPECT_EQ(out.value, 99);
}

// 2. Push then pop: push one item, pop it, values match, second pop on empty buffer returns false.
TEST(RingBufferTest, PushThenPop) {
    ticker_fast::RingBuffer<TestItem, 4> rb;
    TestItem out{0};

    EXPECT_TRUE(rb.try_push(TestItem{42}));
    EXPECT_EQ(rb.size(), 1U);

    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 42);
    EXPECT_EQ(rb.size(), 0U);

    EXPECT_FALSE(rb.try_pop(out));
}

// 3. Fill to capacity: push exactly N items successfully, the (N+1)th try_push fails.
TEST(RingBufferTest, FillToCapacity) {
    constexpr std::size_t kCap = 4;
    ticker_fast::RingBuffer<TestItem, kCap> rb;

    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(rb.try_push(TestItem{i}));
    }

    EXPECT_EQ(rb.size(), kCap);

    // Buffer is full; (N+1)th push must fail without modifying buffer state.
    EXPECT_FALSE(rb.try_push(TestItem{999}));
    EXPECT_EQ(rb.size(), kCap);
}

// 4. Order preserved: FIFO ordering is maintained across multiple push/pop calls.
TEST(RingBufferTest, FifoOrderPreserved) {
    ticker_fast::RingBuffer<TestItem, 4> rb;

    EXPECT_TRUE(rb.try_push(TestItem{100}));
    EXPECT_TRUE(rb.try_push(TestItem{200}));
    EXPECT_TRUE(rb.try_push(TestItem{300}));

    TestItem out{0};
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 100);

    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 200);

    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 300);

    EXPECT_FALSE(rb.try_pop(out));
}

// 5. Wraparound: with capacity 4, push 4, pop 2, push 2 more, pop all remaining.
// Repeat for at least two full wraps to guarantee circular indexing works seamlessly.
TEST(RingBufferTest, WraparoundMultipleCycles) {
    constexpr std::size_t kCap = 4;
    ticker_fast::RingBuffer<TestItem, kCap> rb;
    TestItem out{0};

    // --- Wrap cycle 1 ---
    // Push 4 items: [1, 2, 3, 4] -> head=4, tail=0
    for (int i = 1; i <= 4; ++i) {
        EXPECT_TRUE(rb.try_push(TestItem{i}));
    }
    EXPECT_EQ(rb.size(), 4U);

    // Pop 2 items: 1, 2 -> head=4, tail=2
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 1);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 2);
    EXPECT_EQ(rb.size(), 2U);

    // Push 2 more items: [5, 6] -> head=6, tail=2 (wraps to physical slots 0 and 1)
    EXPECT_TRUE(rb.try_push(TestItem{5}));
    EXPECT_TRUE(rb.try_push(TestItem{6}));
    EXPECT_EQ(rb.size(), 4U);

    // Pop all remaining 4 items: 3, 4, 5, 6 -> head=6, tail=6
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 3);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 4);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 5);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 6);
    EXPECT_EQ(rb.size(), 0U);
    EXPECT_FALSE(rb.try_pop(out));

    // --- Wrap cycle 2 ---
    // Push 4 items: [7, 8, 9, 10] -> head=10, tail=6
    for (int i = 7; i <= 10; ++i) {
        EXPECT_TRUE(rb.try_push(TestItem{i}));
    }
    EXPECT_EQ(rb.size(), 4U);

    // Pop 2 items: 7, 8 -> head=10, tail=8
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 7);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 8);
    EXPECT_EQ(rb.size(), 2U);

    // Push 2 more items: [11, 12] -> head=12, tail=8 (wraps to physical slots 2 and 3)
    EXPECT_TRUE(rb.try_push(TestItem{11}));
    EXPECT_TRUE(rb.try_push(TestItem{12}));
    EXPECT_EQ(rb.size(), 4U);

    // Pop all remaining 4 items: 9, 10, 11, 12 -> head=12, tail=12
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 9);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 10);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 11);
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(out.value, 12);
    EXPECT_EQ(rb.size(), 0U);
    EXPECT_FALSE(rb.try_pop(out));
}

// 6. Near-UINT64_MAX counters:
// In C++, unsigned integer overflow is well-defined: operations wrap modulo 2^64.
// When an unsigned 64-bit counter reaches UINT64_MAX and increments, it wraps back to 0.
// Because capacity N is a power of 2, (i & (N - 1)) produces continuous and valid slot indices
// across the overflow boundary:
//   (UINT64_MAX - 1) & 3 == 2
//   UINT64_MAX       & 3 == 3
//   (UINT64_MAX + 1) & 3 == 0 & 3 == 0
// Furthermore, unsigned distance calculation (head - tail) correctly computes occupancy
// across the wrap boundary. For example, if head=1 and tail=UINT64_MAX, (1 - UINT64_MAX) == 2
// under modulo 2^64 arithmetic.
// This mathematical property is why the RingBuffer can use monotonically increasing uint64_t
// counters that "count forever" without requiring manual resets or conditionals.
TEST(RingBufferTest, MaskingNearUint64Max) {
    constexpr std::uint64_t kCap = 4ULL;
    constexpr std::uint64_t kMask = kCap - 1ULL;

    // Direct index wrapping at boundary
    EXPECT_EQ((UINT64_MAX - 2ULL) & kMask, 1ULL);
    EXPECT_EQ((UINT64_MAX - 1ULL) & kMask, 2ULL);
    EXPECT_EQ(UINT64_MAX & kMask, 3ULL);

    // Increment past UINT64_MAX wraps to 0 in unsigned arithmetic
    const std::uint64_t wrapped_zero = UINT64_MAX + 1ULL;
    EXPECT_EQ(wrapped_zero, 0ULL);
    EXPECT_EQ(wrapped_zero & kMask, 0ULL);

    const std::uint64_t wrapped_one = wrapped_zero + 1ULL;
    EXPECT_EQ(wrapped_one, 1ULL);
    EXPECT_EQ(wrapped_one & kMask, 1ULL);

    // Verify distance computation across the overflow boundary:
    // Suppose tail is UINT64_MAX - 1 and head wrapped around to 2.
    // Slots occupied: (UINT64_MAX - 1), UINT64_MAX, 0, 1 -> exactly 4 slots.
    const std::uint64_t tail_before_overflow = UINT64_MAX - 1ULL;
    const std::uint64_t head_after_overflow = 2ULL;
    const std::uint64_t distance = head_after_overflow - tail_before_overflow;
    EXPECT_EQ(distance, 4ULL);
}

// 7. Works with MarketTick: confirm RingBuffer works correctly with the real protocol type.
TEST(RingBufferTest, WorksWithMarketTick) {
    ticker_fast::RingBuffer<ticker_fast::MarketTick, 4> rb;

    ticker_fast::MarketTick tick{};
    tick.timestamp_ns = 1710000000123456789ULL;
    tick.sequence = 1001ULL;
    std::memcpy(tick.ticker, "NVDA\0\0\0", ticker_fast::kTickerLength);
    tick.bid_price = 890.50;
    tick.ask_price = 890.65;
    tick.volume = 12000U;
    for (std::size_t i = 0; i < sizeof(tick.reserved); ++i) {
        tick.reserved[i] = static_cast<std::uint8_t>(0xBB);
    }

    EXPECT_TRUE(rb.try_push(tick));
    EXPECT_EQ(rb.size(), 1U);

    ticker_fast::MarketTick out{};
    EXPECT_TRUE(rb.try_pop(out));
    EXPECT_EQ(rb.size(), 0U);

    EXPECT_EQ(out.timestamp_ns, tick.timestamp_ns);
    EXPECT_EQ(out.sequence, tick.sequence);
    EXPECT_EQ(std::memcmp(out.ticker, tick.ticker, ticker_fast::kTickerLength), 0);
    EXPECT_DOUBLE_EQ(out.bid_price, tick.bid_price);
    EXPECT_DOUBLE_EQ(out.ask_price, tick.ask_price);
    EXPECT_EQ(out.volume, tick.volume);
    EXPECT_EQ(std::memcmp(out.reserved, tick.reserved, sizeof(tick.reserved)), 0);

    // After popping, the buffer should be empty again
    EXPECT_FALSE(rb.try_pop(out));
}

}  // namespace
