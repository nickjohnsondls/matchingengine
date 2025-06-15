#include <gtest/gtest.h>
#include "core/order.hpp"
#include "core/orderbook.hpp"
#include <thread>
#include <chrono>

using namespace micromatch::core;

// Test fixture for Order tests
class OrderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Common setup if needed
    }
};

// Test basic order construction
TEST_F(OrderTest, BasicConstruction)
{
    Order order(12345, 100, 1234560000, 1000, Side::BUY, 999);

    EXPECT_EQ(order.order_id, 12345);
    EXPECT_EQ(order.symbol_id, 100);
    EXPECT_EQ(order.price, 1234560000); // $1234.56
    EXPECT_EQ(order.quantity, 1000);
    EXPECT_EQ(order.executed_quantity, 0);
    EXPECT_EQ(order.client_id, 999);
    EXPECT_EQ(order.side, Side::BUY);
    EXPECT_EQ(order.type, OrderType::LIMIT);
    EXPECT_EQ(order.status, OrderStatus::NEW);
    EXPECT_EQ(order.tif, TimeInForce::DAY);
    EXPECT_TRUE(order.is_buy());
    EXPECT_FALSE(order.is_sell());
}

// Test order size is exactly 64 bytes
TEST_F(OrderTest, CacheLineSize)
{
    EXPECT_EQ(sizeof(Order), 64);

    // Test alignment
    Order order;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&order) % 64, 0);
}

// Test remaining quantity calculation
TEST_F(OrderTest, RemainingQuantity)
{
    Order order(1, 100, 1000000, 100, Side::BUY);

    EXPECT_EQ(order.remaining_quantity(), 100);

    order.execute(30);
    EXPECT_EQ(order.remaining_quantity(), 70);
    EXPECT_EQ(order.executed_quantity, 30);
    EXPECT_EQ(order.status, OrderStatus::PARTIALLY_FILLED);

    order.execute(70);
    EXPECT_EQ(order.remaining_quantity(), 0);
    EXPECT_EQ(order.executed_quantity, 100);
    EXPECT_EQ(order.status, OrderStatus::FILLED);
    EXPECT_TRUE(order.is_filled());
}

// Test order matching logic
TEST_F(OrderTest, OrderMatching)
{
    // Buy at $100.00, Sell at $99.50 - should match
    Order buy_order(1, 100, 1000000, 100, Side::BUY);
    Order sell_order(2, 100, 995000, 100, Side::SELL);

    EXPECT_TRUE(buy_order.can_match(sell_order));
    EXPECT_TRUE(sell_order.can_match(buy_order));

    // Buy at $99.00, Sell at $99.50 - should NOT match
    Order buy_order2(3, 100, 990000, 100, Side::BUY);
    Order sell_order2(4, 100, 995000, 100, Side::SELL);

    EXPECT_FALSE(buy_order2.can_match(sell_order2));
    EXPECT_FALSE(sell_order2.can_match(buy_order2));

    // Same side orders should never match
    Order buy_order3(5, 100, 1000000, 100, Side::BUY);
    Order buy_order4(6, 100, 1000000, 100, Side::BUY);

    EXPECT_FALSE(buy_order3.can_match(buy_order4));

    // Different symbols should never match
    Order order_sym1(7, 100, 1000000, 100, Side::BUY);
    Order order_sym2(8, 200, 1000000, 100, Side::SELL);

    EXPECT_FALSE(order_sym1.can_match(order_sym2));
}

// Test timestamp generation
TEST_F(OrderTest, TimestampGeneration)
{
    auto start = std::chrono::steady_clock::now();
    Order order1(1, 100, 1000000, 100, Side::BUY);

    // Small delay
    std::this_thread::sleep_for(std::chrono::microseconds(10));

    Order order2(2, 100, 1000000, 100, Side::BUY);
    auto end = std::chrono::steady_clock::now();

    // Verify timestamps are set and increasing
    EXPECT_GT(order1.timestamp_ns, 0);
    EXPECT_GT(order2.timestamp_ns, 0);
    EXPECT_GT(order2.timestamp_ns, order1.timestamp_ns);

    // Verify timestamps are within reasonable bounds
    auto start_ns = start.time_since_epoch().count();
    auto end_ns = end.time_since_epoch().count();

    EXPECT_GE(order1.timestamp_ns, start_ns);
    EXPECT_LE(order2.timestamp_ns, end_ns);
}

// Test Trade structure
TEST_F(OrderTest, TradeConstruction)
{
    Order buy(1, 100, 1000000, 100, Side::BUY);
    Order sell(2, 100, 999000, 100, Side::SELL);

    // Buy is aggressive, sell is passive
    Trade trade(12345, buy, sell, 999000, 50);

    EXPECT_EQ(trade.trade_id, 12345);
    EXPECT_EQ(trade.aggressive_order_id, 1);
    EXPECT_EQ(trade.passive_order_id, 2);
    EXPECT_EQ(trade.buy_order_id(), 1);
    EXPECT_EQ(trade.sell_order_id(), 2);
    EXPECT_EQ(trade.symbol_id, 100);
    EXPECT_EQ(trade.price, 999000);
    EXPECT_EQ(trade.quantity, 50);
    EXPECT_EQ(trade.side, Side::BUY);
    EXPECT_FALSE(trade.is_maker_buy); // Maker was sell
    EXPECT_GT(trade.timestamp_ns, 0);
}

// Test Trade size is exactly 64 bytes
TEST_F(OrderTest, TradeCacheLineSize)
{
    EXPECT_EQ(sizeof(Trade), 64);

    // Test alignment
    Trade trade(1, Order(), Order(), 0, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&trade) % 64, 0);
}

// Test fixed-point price representation
TEST_F(OrderTest, FixedPointPrices)
{
    // Test various price representations
    Order order1(1, 100, 1234567890, 100, Side::BUY); // $1234.567890
    Order order2(2, 100, 1000000, 100, Side::BUY);    // $1.000000
    Order order3(3, 100, 999999, 100, Side::BUY);     // $0.999999
    Order order4(4, 100, -1000000, 100, Side::BUY);   // $-1.000000 (for spreads)

    EXPECT_EQ(order1.price, 1234567890);
    EXPECT_EQ(order2.price, 1000000);
    EXPECT_EQ(order3.price, 999999);
    EXPECT_EQ(order4.price, -1000000);
}

// Test all order types
TEST_F(OrderTest, OrderTypes)
{
    Order order;

    order.type = OrderType::MARKET;
    EXPECT_EQ(static_cast<uint8_t>(order.type), 0);

    order.type = OrderType::LIMIT;
    EXPECT_EQ(static_cast<uint8_t>(order.type), 1);

    order.type = OrderType::STOP;
    EXPECT_EQ(static_cast<uint8_t>(order.type), 2);

    order.type = OrderType::STOP_LIMIT;
    EXPECT_EQ(static_cast<uint8_t>(order.type), 3);
}

// Test all time in force values
TEST_F(OrderTest, TimeInForce)
{
    Order order;

    order.tif = TimeInForce::DAY;
    EXPECT_EQ(static_cast<uint8_t>(order.tif), 0);

    order.tif = TimeInForce::GTC;
    EXPECT_EQ(static_cast<uint8_t>(order.tif), 1);

    order.tif = TimeInForce::IOC;
    EXPECT_EQ(static_cast<uint8_t>(order.tif), 2);

    order.tif = TimeInForce::FOK;
    EXPECT_EQ(static_cast<uint8_t>(order.tif), 3);
}

// Performance test - measure order creation time
TEST_F(OrderTest, PerformanceOrderCreation)
{
    constexpr int NUM_ORDERS = 1000000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_ORDERS; ++i)
    {
        Order order(i, 100, 1000000 + i, 100, Side::BUY);
        // Prevent optimization
        asm volatile("" : : "r,m"(order.order_id) : "memory");
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double ns_per_order = static_cast<double>(duration.count()) / NUM_ORDERS;

    std::cout << "Order creation time: " << ns_per_order << " ns/order" << std::endl;
    std::cout << "Orders per second: " << (1e9 / ns_per_order) << std::endl;

    // Should be able to create at least 10M orders per second
    EXPECT_LT(ns_per_order, 100); // Less than 100ns per order
}