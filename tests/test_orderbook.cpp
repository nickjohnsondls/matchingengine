#include <gtest/gtest.h>
#include "core/orderbook.hpp"
#include <vector>
#include <algorithm>
#include <random>

using namespace micromatch::core;

class OrderBookTest : public ::testing::Test
{
protected:
    std::unique_ptr<IOrderBook> book;
    uint64_t next_order_id = 1;

    void SetUp() override
    {
        book = create_order_book(1); // Symbol ID = 1
    }

    Order create_order(Side side, int64_t price, uint32_t quantity)
    {
        Order order;
        order.order_id = next_order_id++;
        order.symbol_id = 1;
        order.side = side;
        order.price = price;
        order.quantity = quantity;
        order.timestamp_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        return order;
    }
};

// Basic functionality tests
TEST_F(OrderBookTest, EmptyBook)
{
    EXPECT_FALSE(book->best_bid().has_value());
    EXPECT_FALSE(book->best_ask().has_value());
    EXPECT_EQ(book->total_orders(), 0);
    EXPECT_EQ(book->symbol_id(), 1);
}

TEST_F(OrderBookTest, AddSingleBuyOrder)
{
    auto order = create_order(Side::BUY, 100, 10);
    auto trades = book->add_order(order);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book->total_orders(), 1);
    EXPECT_TRUE(book->best_bid().has_value());
    EXPECT_EQ(*book->best_bid(), 100);
    EXPECT_FALSE(book->best_ask().has_value());
}

TEST_F(OrderBookTest, AddSingleSellOrder)
{
    auto order = create_order(Side::SELL, 101, 10);
    auto trades = book->add_order(order);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book->total_orders(), 1);
    EXPECT_FALSE(book->best_bid().has_value());
    EXPECT_TRUE(book->best_ask().has_value());
    EXPECT_EQ(*book->best_ask(), 101);
}

TEST_F(OrderBookTest, MultipleBuyOrders)
{
    book->add_order(create_order(Side::BUY, 99, 10));
    book->add_order(create_order(Side::BUY, 100, 20));
    book->add_order(create_order(Side::BUY, 98, 15));

    EXPECT_EQ(book->total_orders(), 3);
    EXPECT_EQ(*book->best_bid(), 100); // Highest buy price
    EXPECT_EQ(book->volume_at_price(100, Side::BUY), 20);
    EXPECT_EQ(book->volume_at_price(99, Side::BUY), 10);
    EXPECT_EQ(book->volume_at_price(98, Side::BUY), 15);
}

TEST_F(OrderBookTest, MultipleSellOrders)
{
    book->add_order(create_order(Side::SELL, 102, 10));
    book->add_order(create_order(Side::SELL, 101, 20));
    book->add_order(create_order(Side::SELL, 103, 15));

    EXPECT_EQ(book->total_orders(), 3);
    EXPECT_EQ(*book->best_ask(), 101); // Lowest sell price
    EXPECT_EQ(book->volume_at_price(101, Side::SELL), 20);
    EXPECT_EQ(book->volume_at_price(102, Side::SELL), 10);
    EXPECT_EQ(book->volume_at_price(103, Side::SELL), 15);
}

// Matching tests
TEST_F(OrderBookTest, SimpleMatch)
{
    // Add sell order first
    book->add_order(create_order(Side::SELL, 100, 10));

    // Add matching buy order
    auto buy_order = create_order(Side::BUY, 100, 10);
    auto trades = book->add_order(buy_order);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 10);
    EXPECT_EQ(trades[0].aggressive_order_id, buy_order.order_id);
    EXPECT_EQ(trades[0].side, Side::BUY);
    EXPECT_TRUE(trades[0].is_maker_buy == false); // Maker was sell

    EXPECT_EQ(book->total_orders(), 0); // Both orders fully filled
}

TEST_F(OrderBookTest, PartialMatch)
{
    // Add sell order
    auto sell_order = create_order(Side::SELL, 100, 20);
    book->add_order(sell_order);

    // Add smaller buy order
    auto buy_order = create_order(Side::BUY, 100, 15);
    auto trades = book->add_order(buy_order);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 15);
    EXPECT_EQ(book->total_orders(), 1);                   // Sell order partially filled
    EXPECT_EQ(book->volume_at_price(100, Side::SELL), 5); // 20 - 15 = 5
}

TEST_F(OrderBookTest, MultipleMatches)
{
    // Add multiple sell orders
    book->add_order(create_order(Side::SELL, 100, 10));
    book->add_order(create_order(Side::SELL, 100, 15));
    book->add_order(create_order(Side::SELL, 101, 20));

    // Add large buy order that crosses multiple levels
    auto buy_order = create_order(Side::BUY, 101, 30);
    auto trades = book->add_order(buy_order);

    ASSERT_EQ(trades.size(), 2); // Should match both orders at 100
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 10);
    EXPECT_EQ(trades[1].price, 100);
    EXPECT_EQ(trades[1].quantity, 15);

    // Remaining 5 shares should be on the book
    EXPECT_EQ(book->total_orders(), 2); // Buy order + sell at 101
    EXPECT_EQ(book->volume_at_price(101, Side::BUY), 5);
}

TEST_F(OrderBookTest, PriceTimePriority)
{
    // Add orders at same price - FIFO should apply
    auto sell1 = create_order(Side::SELL, 100, 10);
    auto sell2 = create_order(Side::SELL, 100, 10);
    auto sell3 = create_order(Side::SELL, 100, 10);

    book->add_order(sell1);
    book->add_order(sell2);
    book->add_order(sell3);

    // Match against first order
    auto buy_order = create_order(Side::BUY, 100, 10);
    auto trades = book->add_order(buy_order);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].passive_order_id, sell1.order_id); // First in, first matched
    EXPECT_EQ(book->volume_at_price(100, Side::SELL), 20); // 2 orders remaining
}

TEST_F(OrderBookTest, AggressivePriceImprovement)
{
    // Sell order at 100
    book->add_order(create_order(Side::SELL, 100, 10));

    // Buy order at 105 (willing to pay more)
    auto buy_order = create_order(Side::BUY, 105, 10);
    auto trades = book->add_order(buy_order);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, 100); // Trade at passive order price
}

// Cancel order tests
TEST_F(OrderBookTest, CancelOrder)
{
    auto order = create_order(Side::BUY, 100, 10);
    book->add_order(order);

    EXPECT_TRUE(book->cancel_order(order.order_id));
    EXPECT_EQ(book->total_orders(), 0);
    EXPECT_FALSE(book->best_bid().has_value());
}

TEST_F(OrderBookTest, CancelNonExistentOrder)
{
    EXPECT_FALSE(book->cancel_order(999));
}

TEST_F(OrderBookTest, CancelFromMiddleOfQueue)
{
    auto order1 = create_order(Side::BUY, 100, 10);
    auto order2 = create_order(Side::BUY, 100, 20);
    auto order3 = create_order(Side::BUY, 100, 30);

    book->add_order(order1);
    book->add_order(order2);
    book->add_order(order3);

    EXPECT_TRUE(book->cancel_order(order2.order_id));
    EXPECT_EQ(book->volume_at_price(100, Side::BUY), 40); // 10 + 30
    EXPECT_EQ(book->order_count_at_price(100, Side::BUY), 2);
}

// Modify order tests
TEST_F(OrderBookTest, ModifyOrderPrice)
{
    auto order = create_order(Side::BUY, 100, 10);
    book->add_order(order);

    auto modified = book->modify_order(order.order_id, 101, 10);
    ASSERT_TRUE(modified.has_value());
    EXPECT_EQ(modified->price, 101);
    EXPECT_EQ(*book->best_bid(), 101);
}

TEST_F(OrderBookTest, ModifyOrderQuantity)
{
    auto order = create_order(Side::BUY, 100, 10);
    book->add_order(order);

    auto modified = book->modify_order(order.order_id, 100, 20);
    ASSERT_TRUE(modified.has_value());
    EXPECT_EQ(modified->quantity, 20);
    EXPECT_EQ(book->volume_at_price(100, Side::BUY), 20);
}

TEST_F(OrderBookTest, ModifyNonExistentOrder)
{
    auto modified = book->modify_order(999, 100, 10);
    EXPECT_FALSE(modified.has_value());
}

// Market data tests
TEST_F(OrderBookTest, MarketDataSnapshot)
{
    book->add_order(create_order(Side::BUY, 99, 100));
    book->add_order(create_order(Side::BUY, 99, 50));
    book->add_order(create_order(Side::SELL, 101, 75));
    book->add_order(create_order(Side::SELL, 101, 25));

    MarketDataSnapshot snapshot(*book);

    EXPECT_EQ(snapshot.symbol_id, 1);
    EXPECT_EQ(*snapshot.best_bid, 99);
    EXPECT_EQ(*snapshot.best_ask, 101);
    EXPECT_EQ(snapshot.bid_volume, 150);
    EXPECT_EQ(snapshot.ask_volume, 100);
    EXPECT_EQ(snapshot.bid_orders, 2);
    EXPECT_EQ(snapshot.ask_orders, 2);
}

// Stress tests
TEST_F(OrderBookTest, LargeNumberOfOrders)
{
    const int num_orders = 10000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(90, 110);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 100);

    // Add many orders
    for (int i = 0; i < num_orders; ++i)
    {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        book->add_order(create_order(side, price_dist(rng), qty_dist(rng)));
    }

    // Book should handle large number of orders
    EXPECT_GT(book->total_orders(), 0);
    EXPECT_TRUE(book->best_bid().has_value());
    EXPECT_TRUE(book->best_ask().has_value());
    EXPECT_LT(*book->best_bid(), *book->best_ask()); // Valid spread
}

TEST_F(OrderBookTest, MatchingPerformance)
{
    // Pre-populate book with orders
    for (int i = 0; i < 1000; ++i)
    {
        book->add_order(create_order(Side::SELL, 100 + i, 100));
    }

    // Time a large matching order
    auto start = std::chrono::high_resolution_clock::now();

    auto buy_order = create_order(Side::BUY, 1100, 50000);
    auto trades = book->add_order(buy_order);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_EQ(trades.size(), 500);      // Should match 500 orders (50000 / 100)
    EXPECT_LT(duration.count(), 10000); // Should complete in < 10ms
}

// Edge cases
TEST_F(OrderBookTest, ZeroQuantityOrder)
{
    auto order = create_order(Side::BUY, 100, 0);
    auto trades = book->add_order(order);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book->total_orders(), 0);
}

TEST_F(OrderBookTest, NegativePriceOrder)
{
    auto order = create_order(Side::BUY, -100, 10);
    auto trades = book->add_order(order);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book->total_orders(), 0);
}

TEST_F(OrderBookTest, DuplicateOrderId)
{
    auto order1 = create_order(Side::BUY, 100, 10);
    book->add_order(order1);

    // Try to add order with same ID
    Order order2 = order1;
    order2.price = 101;
    auto trades = book->add_order(order2);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book->total_orders(), 1);
    EXPECT_EQ(*book->best_bid(), 100); // Original order unchanged
}

TEST_F(OrderBookTest, ClearBook)
{
    // Add some orders
    book->add_order(create_order(Side::BUY, 100, 10));
    book->add_order(create_order(Side::SELL, 101, 10));

    EXPECT_EQ(book->total_orders(), 2);

    book->clear();

    EXPECT_EQ(book->total_orders(), 0);
    EXPECT_FALSE(book->best_bid().has_value());
    EXPECT_FALSE(book->best_ask().has_value());
}
