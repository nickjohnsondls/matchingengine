#include <gtest/gtest.h>
#include "network/market_data.hpp"
#include "network/feed_simulator.hpp"
#include "network/arbitrage_detector.hpp"
#include "network/feed_handler.hpp"
#include "core/matching_engine.hpp"
#include <thread>
#include <chrono>
#include <atomic>

using namespace micromatch;

class NetworkTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        matching_engine = core::create_matching_engine();
        matching_engine->start();
    }

    void TearDown() override
    {
        matching_engine->stop();
    }

    std::unique_ptr<core::IMatchingEngine> matching_engine;
};

TEST_F(NetworkTest, FeedSimulatorBasic)
{
    network::FeedSimulator feed('A');
    std::atomic<int> message_count{0};

    feed.set_callback([&](const network::MarketDataUpdate &update, const network::FeedStats &stats)
                      {
        message_count++;
        EXPECT_EQ(update.type, network::UpdateType::QUOTE);
        EXPECT_EQ(update.quote.symbol_id, 1);
        EXPECT_EQ(update.quote.feed_id, 'A'); });

    feed.start();

    // Publish some quotes
    for (int i = 0; i < 10; ++i)
    {
        feed.publish_quote(1, 10000 + i, 10001 + i, 100, 100);
    }

    // Wait for messages to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    feed.stop();

    // Allow for some messages to be dropped or delayed
    EXPECT_GE(message_count.load(), 8);
    EXPECT_LE(message_count.load(), 10);

    auto stats = feed.get_stats();
    EXPECT_GE(stats.messages_received, 8);
    EXPECT_LE(stats.messages_received, 10);
    EXPECT_GT(stats.average_latency_us(), 0);
}

TEST_F(NetworkTest, FeedJitterInjection)
{
    network::FeedConfig config;
    config.base_latency_ns = 1000;   // 1μs
    config.jitter_normal_ns = 500;   // 0.5μs
    config.jitter_spike_ns = 100000; // 100μs
    config.spike_probability = 0.1;  // 10% spike chance

    network::FeedSimulator feed('A', config);
    std::vector<uint64_t> latencies;

    feed.set_callback([&](const network::MarketDataUpdate &update, const network::FeedStats &stats)
                      {
        if (stats.messages_received > 1) {
            latencies.push_back(stats.latency_max_ns);
        } });

    feed.start();

    // Publish many quotes to observe jitter
    for (int i = 0; i < 100; ++i)
    {
        feed.publish_quote(1, 10000, 10001, 100, 100);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    feed.stop();

    // Check that we see some high-latency spikes
    auto max_latency = *std::max_element(latencies.begin(), latencies.end());
    EXPECT_GT(max_latency, 50000); // Should see spikes > 50μs
}

TEST_F(NetworkTest, ArbitrageDetection)
{
    network::ArbitrageDetector detector;
    std::atomic<int> arbitrage_count{0};

    detector.set_callback([&](const network::ArbitrageOpportunity &opp)
                          {
        arbitrage_count++;
        EXPECT_GT(opp.profit_basis_points(), 0); });

    // Simulate quotes from two feeds with price discrepancy
    network::Quote quote_a(1, 10000, 10010, 100, 100, 'A'); // Bid: 100.00, Ask: 100.10
    network::Quote quote_b(1, 10020, 10030, 100, 100, 'B'); // Bid: 100.20, Ask: 100.30

    network::MarketDataUpdate update_a(quote_a);
    network::MarketDataUpdate update_b(quote_b);

    detector.on_feed_update('A', update_a);
    detector.on_feed_update('B', update_b);

    EXPECT_EQ(arbitrage_count.load(), 1);

    auto stats = detector.get_stats();
    EXPECT_EQ(stats.opportunities_detected, 1);
    EXPECT_EQ(stats.profitable_opportunities, 1);
    EXPECT_GT(stats.average_profit_bps(), 0);
}

TEST_F(NetworkTest, ArbitrageWithNoOpportunity)
{
    network::ArbitrageDetector detector;
    std::atomic<int> arbitrage_count{0};

    detector.set_callback([&](const network::ArbitrageOpportunity &opp)
                          { arbitrage_count++; });

    // Quotes with no arbitrage opportunity
    network::Quote quote_a(1, 10000, 10010, 100, 100, 'A'); // Bid: 100.00, Ask: 100.10
    network::Quote quote_b(1, 9990, 10005, 100, 100, 'B');  // Bid: 99.90, Ask: 100.05

    network::MarketDataUpdate update_a(quote_a);
    network::MarketDataUpdate update_b(quote_b);

    detector.on_feed_update('A', update_a);
    detector.on_feed_update('B', update_b);

    // Should detect price difference but not profitable arbitrage
    EXPECT_EQ(arbitrage_count.load(), 1);

    auto opportunities = detector.get_recent_opportunities();
    EXPECT_EQ(opportunities.size(), 1);
    EXPECT_EQ(opportunities[0].profit_basis_points(), 0.0);
}

TEST_F(NetworkTest, FeedHandlerIntegration)
{
    auto engine = core::create_matching_engine();
    engine->start();
    network::FeedHandler handler(std::move(engine));

    handler.start();

    // Publish quotes to both feeds
    for (int i = 0; i < 10; ++i)
    {
        handler.publish_quote(1, 10000 + i * 10, 10010 + i * 10, 100, 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Simulate market volatility
    handler.set_volatile_market(true);

    for (int i = 0; i < 10; ++i)
    {
        handler.publish_quote(1, 10100 + i * 50, 10110 + i * 50, 200, 200);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    handler.set_volatile_market(false);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    handler.stop();

    // Check that arbitrage was detected
    auto opportunities = handler.get_recent_arbitrage();
    EXPECT_GT(opportunities.size(), 0);

    // Print stats for visibility
    handler.print_stats();
}

TEST_F(NetworkTest, VolatileMarketJitter)
{
    network::FeedConfig config;
    config.volatile_market = true;
    config.volatile_jitter_multiplier = 100;

    network::FeedSimulator feed('A', config);
    std::atomic<uint64_t> max_latency{0};

    feed.set_callback([&](const network::MarketDataUpdate &update, const network::FeedStats &stats)
                      {
        uint64_t current_max = stats.latency_max_ns;
        uint64_t prev_max = max_latency.load();
        while (prev_max < current_max && 
               !max_latency.compare_exchange_weak(prev_max, current_max)); });

    feed.start();

    // Generate traffic during volatile market
    for (int i = 0; i < 50; ++i)
    {
        feed.publish_quote(1, 10000, 10001, 100, 100);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    feed.stop();

    // During volatility, we should see much higher jitter
    EXPECT_GT(max_latency.load(), 100000); // > 100μs
}

TEST_F(NetworkTest, MultiSymbolArbitrage)
{
    network::ArbitrageDetector detector;
    std::map<uint64_t, int> arbitrage_by_symbol;

    detector.set_callback([&](const network::ArbitrageOpportunity &opp)
                          { arbitrage_by_symbol[opp.symbol_id]++; });

    // Test multiple symbols
    for (uint64_t symbol = 1; symbol <= 3; ++symbol)
    {
        network::Quote quote_a(symbol, 10000 * symbol, 10010 * symbol, 100, 100, 'A');
        network::Quote quote_b(symbol, 10020 * symbol, 10030 * symbol, 100, 100, 'B');

        detector.on_feed_update('A', network::MarketDataUpdate(quote_a));
        detector.on_feed_update('B', network::MarketDataUpdate(quote_b));
    }

    EXPECT_EQ(arbitrage_by_symbol.size(), 3);
    for (const auto &[symbol, count] : arbitrage_by_symbol)
    {
        EXPECT_EQ(count, 1);
    }
}

TEST_F(NetworkTest, LatencyDifferenceTracking)
{
    network::ArbitrageDetector detector;

    // Create quotes with different timestamps
    network::Quote quote_a(1, 10000, 10010, 100, 100, 'A');
    std::this_thread::sleep_for(std::chrono::microseconds(500)); // 500μs delay
    network::Quote quote_b(1, 10020, 10030, 100, 100, 'B');

    detector.on_feed_update('A', network::MarketDataUpdate(quote_a));
    detector.on_feed_update('B', network::MarketDataUpdate(quote_b));

    auto opportunities = detector.get_recent_opportunities();
    EXPECT_EQ(opportunities.size(), 1);
    EXPECT_GT(opportunities[0].latency_difference_ns, 400000); // > 400μs

    auto stats = detector.get_stats();
    EXPECT_GT(stats.average_latency_diff_us(), 400);
}