#include <gtest/gtest.h>
#include "core/matching_engine.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <iostream>

using namespace micromatch::core;
using namespace std::chrono_literals;

class MatchingEngineTest : public ::testing::Test
{
protected:
    std::unique_ptr<IMatchingEngine> engine;
    std::vector<Trade> captured_trades;
    std::vector<std::pair<Order, bool>> captured_orders;
    uint64_t next_order_id = 1;

    void SetUp() override
    {
        engine = create_matching_engine();

        // Set up callbacks
        engine->set_trade_callback([this](const Trade &trade)
                                   { captured_trades.push_back(trade); });

        engine->set_order_callback([this](const Order &order, bool accepted)
                                   { captured_orders.push_back({order, accepted}); });

        // Register test symbols
        engine->register_symbol(1);
        engine->register_symbol(2);

        // Start the engine
        engine->start();

        // Give engine time to start
        std::this_thread::sleep_for(10ms);
    }

    void TearDown() override
    {
        if (engine->is_running())
        {
            engine->stop();
        }
    }

    Order create_order(uint64_t symbol_id, Side side, int64_t price, uint32_t quantity)
    {
        Order order;
        order.order_id = next_order_id++;
        order.symbol_id = symbol_id;
        order.side = side;
        order.price = price;
        order.quantity = quantity;
        order.timestamp_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        return order;
    }

    void wait_for_trades(size_t expected_count, std::chrono::milliseconds timeout = 100ms)
    {
        auto start = std::chrono::steady_clock::now();
        while (captured_trades.size() < expected_count)
        {
            if (std::chrono::steady_clock::now() - start > timeout)
            {
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
    }

    void wait_for_orders(size_t expected_count, std::chrono::milliseconds timeout = 100ms)
    {
        auto start = std::chrono::steady_clock::now();
        while (captured_orders.size() < expected_count)
        {
            if (std::chrono::steady_clock::now() - start > timeout)
            {
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
    }
};

// Basic tests
TEST_F(MatchingEngineTest, StartStop)
{
    EXPECT_TRUE(engine->is_running());
    engine->stop();
    EXPECT_FALSE(engine->is_running());
}

TEST_F(MatchingEngineTest, RegisterSymbol)
{
    EXPECT_TRUE(engine->register_symbol(3));
    EXPECT_FALSE(engine->register_symbol(3)); // Already registered

    auto book = engine->get_order_book(3);
    ASSERT_NE(book, nullptr);
    EXPECT_EQ(book->symbol_id(), 3);
}

TEST_F(MatchingEngineTest, UnregisterSymbol)
{
    EXPECT_TRUE(engine->unregister_symbol(2));
    EXPECT_FALSE(engine->unregister_symbol(2)); // Already unregistered

    auto book = engine->get_order_book(2);
    EXPECT_EQ(book, nullptr);
}

// Order submission tests
TEST_F(MatchingEngineTest, SubmitSingleOrder)
{
    auto order = create_order(1, Side::BUY, 100, 10);
    engine->submit_order(order);

    wait_for_orders(1);

    ASSERT_EQ(captured_orders.size(), 1);
    EXPECT_EQ(captured_orders[0].first.order_id, order.order_id);
    EXPECT_TRUE(captured_orders[0].second); // Accepted
}

TEST_F(MatchingEngineTest, SubmitToUnregisteredSymbol)
{
    auto order = create_order(999, Side::BUY, 100, 10);
    engine->submit_order(order);

    wait_for_orders(1);

    ASSERT_EQ(captured_orders.size(), 1);
    EXPECT_FALSE(captured_orders[0].second); // Rejected

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.rejected_orders, 1);
}

// Matching tests
TEST_F(MatchingEngineTest, SimpleMatch)
{
    auto sell_order = create_order(1, Side::SELL, 100, 10);
    auto buy_order = create_order(1, Side::BUY, 100, 10);

    engine->submit_order(sell_order);
    engine->submit_order(buy_order);

    wait_for_trades(1);

    ASSERT_EQ(captured_trades.size(), 1);
    EXPECT_EQ(captured_trades[0].price, 100);
    EXPECT_EQ(captured_trades[0].quantity, 10);
    EXPECT_EQ(captured_trades[0].symbol_id, 1);

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.total_orders, 2);
    EXPECT_EQ(stats.total_trades, 1);
    EXPECT_EQ(stats.total_volume, 10);
}

TEST_F(MatchingEngineTest, MultipleMatches)
{
    // Submit multiple sell orders
    engine->submit_order(create_order(1, Side::SELL, 100, 5));
    engine->submit_order(create_order(1, Side::SELL, 100, 5));
    engine->submit_order(create_order(1, Side::SELL, 100, 5));

    // Submit buy order that matches all
    engine->submit_order(create_order(1, Side::BUY, 100, 15));

    wait_for_trades(3);

    ASSERT_EQ(captured_trades.size(), 3);
    EXPECT_EQ(captured_trades[0].quantity, 5);
    EXPECT_EQ(captured_trades[1].quantity, 5);
    EXPECT_EQ(captured_trades[2].quantity, 5);
}

// Cancel order tests
TEST_F(MatchingEngineTest, CancelOrder)
{
    auto order = create_order(1, Side::BUY, 100, 10);
    engine->submit_order(order);

    wait_for_orders(1);

    engine->cancel_order(1, order.order_id);

    // Give time for cancellation
    std::this_thread::sleep_for(10ms);

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.cancelled_orders, 1);

    // Verify order is cancelled by checking book
    auto book = engine->get_order_book(1);
    EXPECT_EQ(book->total_orders(), 0);
}

// Modify order tests
TEST_F(MatchingEngineTest, ModifyOrder)
{
    auto order = create_order(1, Side::BUY, 100, 10);
    engine->submit_order(order);

    wait_for_orders(1);

    engine->modify_order(1, order.order_id, 101, 20);

    // Give time for modification
    std::this_thread::sleep_for(10ms);

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.modified_orders, 1);

    // Verify modification
    auto book = engine->get_order_book(1);
    EXPECT_EQ(book->best_bid(), 101);
    EXPECT_EQ(book->volume_at_price(101, Side::BUY), 20);
}

// Multi-symbol tests
TEST_F(MatchingEngineTest, MultipleSymbols)
{
    // Submit orders to different symbols
    engine->submit_order(create_order(1, Side::BUY, 100, 10));
    engine->submit_order(create_order(2, Side::BUY, 200, 20));

    wait_for_orders(2);

    // Check each book independently
    auto book1 = engine->get_order_book(1);
    auto book2 = engine->get_order_book(2);

    EXPECT_EQ(book1->best_bid(), 100);
    EXPECT_EQ(book2->best_bid(), 200);
}

// Concurrent tests
TEST_F(MatchingEngineTest, ConcurrentOrders)
{
    const int num_threads = 4;
    const int orders_per_thread = 100;
    std::atomic<int> orders_submitted{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&, i]()
                             {
            for (int j = 0; j < orders_per_thread; ++j) {
                Side side = (j % 2 == 0) ? Side::BUY : Side::SELL;
                int64_t price = 100 + (j % 10);
                engine->submit_order(create_order(1, side, price, 1));
                orders_submitted.fetch_add(1);
            } });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    // Wait for all orders to be processed
    std::this_thread::sleep_for(100ms);

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.total_orders, num_threads * orders_per_thread);
    EXPECT_GT(stats.total_trades, 0); // Should have some matches
}

// Performance test
TEST_F(MatchingEngineTest, Throughput)
{
    const int num_orders = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    // Submit many orders
    for (int i = 0; i < num_orders; ++i)
    {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        int64_t price = 100 + (i % 20) - 10; // Prices from 90 to 110
        engine->submit_order(create_order(1, side, price, 1));
    }

    auto submit_end = std::chrono::high_resolution_clock::now();
    auto submit_duration = std::chrono::duration_cast<std::chrono::milliseconds>(submit_end - start);

    // Wait for processing
    std::this_thread::sleep_for(200ms);

    auto stats = engine->get_stats();
    EXPECT_EQ(stats.total_orders, num_orders);

    std::cout << "Submitted " << num_orders << " orders in " << submit_duration.count() << "ms" << std::endl;
    std::cout << "Throughput: " << (num_orders * 1000.0 / submit_duration.count()) << " orders/sec" << std::endl;
    std::cout << "Generated " << stats.total_trades << " trades" << std::endl;
}

// Edge cases
TEST_F(MatchingEngineTest, StopWithPendingOrders)
{
    // Submit orders
    for (int i = 0; i < 100; ++i)
    {
        engine->submit_order(create_order(1, Side::BUY, 100 + i, 10));
    }

    // Stop immediately
    engine->stop();

    // Engine should process remaining orders before stopping
    auto stats = engine->get_stats();
    EXPECT_GT(stats.total_orders, 0);
}

TEST_F(MatchingEngineTest, ClearAllBooks)
{
    // Add orders to multiple books
    engine->submit_order(create_order(1, Side::BUY, 100, 10));
    engine->submit_order(create_order(2, Side::SELL, 200, 20));

    wait_for_orders(2);

    engine->clear_all_books();

    // Verify books are empty
    auto book1 = engine->get_order_book(1);
    auto book2 = engine->get_order_book(2);

    EXPECT_EQ(book1->total_orders(), 0);
    EXPECT_EQ(book2->total_orders(), 0);
}

TEST_F(MatchingEngineTest, SubmitAfterStop)
{
    engine->stop();

    EXPECT_THROW({ engine->submit_order(create_order(1, Side::BUY, 100, 10)); }, std::runtime_error);
}