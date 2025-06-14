#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <random>
#include "core/order.hpp"
#include "utils/lockfree_queue.hpp"
#include "utils/time_utils.hpp"

using namespace micromatch;

// Test basic order creation and properties
TEST(OrderTest, BasicOrderCreation)
{
    core::Order order(1, 100, 1000000, 100, core::Side::BUY, core::OrderType::LIMIT);

    EXPECT_EQ(order.order_id, 1);
    EXPECT_EQ(order.symbol_id, 100);
    EXPECT_EQ(order.price, 1000000); // $100.00 in fixed point
    EXPECT_EQ(order.quantity, 100);
    EXPECT_EQ(order.remaining_quantity, 100);
    EXPECT_EQ(order.side, core::Side::BUY);
    EXPECT_EQ(order.type, core::OrderType::LIMIT);
    EXPECT_EQ(order.status, core::OrderStatus::NEW);
}

// Test order matching logic
TEST(OrderTest, OrderMatching)
{
    core::Order buy_order(1, 100, 1000000, 100, core::Side::BUY, core::OrderType::LIMIT);
    core::Order sell_order(2, 100, 999000, 100, core::Side::SELL, core::OrderType::LIMIT);

    // Buy at $100, Sell at $99.90 - should match
    EXPECT_TRUE(buy_order.can_match(sell_order));
    EXPECT_TRUE(sell_order.can_match(buy_order));

    // Same side orders should not match
    core::Order buy_order2(3, 100, 1010000, 100, core::Side::BUY, core::OrderType::LIMIT);
    EXPECT_FALSE(buy_order.can_match(buy_order2));
}

// Test SPSC queue performance
TEST(LockFreeQueueTest, SPSCQueueThroughput)
{
    constexpr size_t QUEUE_SIZE = 65536;
    constexpr size_t NUM_ITEMS = 1000000;

    utils::SPSCQueue<uint64_t, QUEUE_SIZE> queue;

    auto start = std::chrono::high_resolution_clock::now();

    // Producer thread
    std::thread producer([&queue, NUM_ITEMS]()
                         {
        for (size_t i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.push(i)) {
                // Spin wait
            }
        } });

    // Consumer thread
    std::thread consumer([&queue, NUM_ITEMS]()
                         {
        size_t count = 0;
        while (count < NUM_ITEMS) {
            auto item = queue.pop();
            if (item.has_value()) {
                EXPECT_EQ(item.value(), count);
                count++;
            }
        } });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double throughput = static_cast<double>(NUM_ITEMS) / duration.count() * 1e6;
    std::cout << "SPSC Queue throughput: " << throughput << " items/second" << std::endl;

    // Should handle at least 10M items/second
    EXPECT_GT(throughput, 10000000);
}

// Test jitter injection
TEST(TimeUtilsTest, JitterInjection)
{
    utils::JitterInjector jitter(50000); // 50μs base latency

    // Test normal conditions
    jitter.set_condition(utils::JitterInjector::MarketCondition::NORMAL);
    std::vector<uint64_t> normal_samples;
    for (int i = 0; i < 1000; ++i)
    {
        normal_samples.push_back(jitter.inject_jitter());
    }

    // Calculate statistics
    double sum = 0;
    for (auto sample : normal_samples)
    {
        sum += sample;
    }
    double mean = sum / normal_samples.size();

    // Mean should be close to base latency
    EXPECT_NEAR(mean, 50000, 5000); // Within 5μs

    // Test extreme conditions
    jitter.set_condition(utils::JitterInjector::MarketCondition::EXTREME);
    uint64_t max_latency = 0;
    for (int i = 0; i < 1000; ++i)
    {
        uint64_t latency = jitter.inject_jitter();
        max_latency = std::max(max_latency, latency);
    }

    // Should see spikes over 1ms
    EXPECT_GT(max_latency, 1000000);
}

// Test TSC timing accuracy
TEST(TimeUtilsTest, TSCAccuracy)
{
    double tsc_freq = utils::TimeUtils::calibrate_tsc_frequency();

    // TSC frequency should be reasonable (1-5 GHz)
    EXPECT_GT(tsc_freq, 1e9);
    EXPECT_LT(tsc_freq, 5e9);

    // Test timing accuracy
    utils::LatencyTracker tracker;
    tracker.start();

    // Busy wait for approximately 1μs
    uint64_t start = utils::TimeUtils::rdtsc();
    while (utils::TimeUtils::rdtsc() - start < tsc_freq / 1e6)
    {
        __builtin_ia32_pause();
    }

    uint64_t cycles = tracker.stop();
    uint64_t ns = utils::TimeUtils::tsc_to_ns(cycles, tsc_freq);

    // Should be close to 1000ns
    EXPECT_NEAR(ns, 1000, 200); // Within 200ns
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}