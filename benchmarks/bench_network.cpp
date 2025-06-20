#include <benchmark/benchmark.h>
#include "network/feed_simulator.hpp"
#include "network/arbitrage_detector.hpp"
#include "network/feed_handler.hpp"
#include "core/matching_engine.hpp"
#include <random>
#include <chrono>

using namespace micromatch;

// Benchmark feed latency under normal conditions
static void BM_FeedLatencyNormal(benchmark::State &state)
{
    network::FeedConfig config;
    config.base_latency_ns = 5000;    // 5μs
    config.jitter_normal_ns = 1000;   // 1μs
    config.jitter_spike_ns = 500000;  // 500μs
    config.spike_probability = 0.001; // 0.1%

    network::FeedSimulator feed('A', config);
    std::atomic<uint64_t> total_latency{0};
    std::atomic<uint64_t> message_count{0};

    feed.set_callback([&](const network::MarketDataUpdate &update, const network::FeedStats &stats)
                      {
        total_latency.fetch_add(stats.latency_max_ns);
        message_count.fetch_add(1); });

    feed.start();

    for (auto _ : state)
    {
        feed.publish_quote(1, 10000, 10001, 100, 100);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    feed.stop();

    state.SetItemsProcessed(message_count.load());
    state.counters["avg_latency_us"] =
        static_cast<double>(total_latency.load()) / message_count.load() / 1000.0;
}

// Benchmark feed latency during volatile market conditions
static void BM_FeedLatencyVolatile(benchmark::State &state)
{
    network::FeedConfig config;
    config.base_latency_ns = 5000;
    config.jitter_normal_ns = 1000;
    config.volatile_market = true;
    config.volatile_jitter_multiplier = 100; // 100x jitter

    network::FeedSimulator feed('A', config);
    std::atomic<uint64_t> total_latency{0};
    std::atomic<uint64_t> message_count{0};
    std::atomic<uint64_t> jitter_events{0};

    feed.set_callback([&](const network::MarketDataUpdate &update, const network::FeedStats &stats)
                      {
        total_latency.fetch_add(stats.latency_max_ns);
        message_count.fetch_add(1);
        if (stats.jitter_events > 0) {
            jitter_events.fetch_add(1);
        } });

    feed.start();

    for (auto _ : state)
    {
        feed.publish_quote(1, 10000, 10001, 100, 100);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    feed.stop();

    state.SetItemsProcessed(message_count.load());
    state.counters["avg_latency_us"] =
        static_cast<double>(total_latency.load()) / message_count.load() / 1000.0;
    state.counters["jitter_events"] = static_cast<double>(jitter_events.load());
}

// Benchmark arbitrage detection performance
static void BM_ArbitrageDetection(benchmark::State &state)
{
    network::ArbitrageDetector detector;
    std::atomic<uint64_t> opportunities{0};

    detector.set_callback([&](const network::ArbitrageOpportunity &opp)
                          { opportunities.fetch_add(1); });

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(9900, 10100);

    for (auto _ : state)
    {
        // Generate random quotes that sometimes create arbitrage
        int64_t base_price = price_dist(rng);

        network::Quote quote_a(1, base_price, base_price + 10, 100, 100, 'A');
        network::Quote quote_b(1, base_price + price_dist(rng) % 30 - 10,
                               base_price + price_dist(rng) % 30, 100, 100, 'B');

        detector.on_feed_update('A', network::MarketDataUpdate(quote_a));
        detector.on_feed_update('B', network::MarketDataUpdate(quote_b));
    }

    state.SetItemsProcessed(state.iterations() * 2); // 2 updates per iteration
    state.counters["opportunities_per_update"] =
        static_cast<double>(opportunities.load()) / (state.iterations() * 2);
}

// Benchmark full system with A/B feeds
static void BM_FullSystemWithFeeds(benchmark::State &state)
{
    auto matching_engine = core::create_matching_engine();
    matching_engine->start();

    network::FeedHandler handler(std::move(matching_engine));
    handler.start();

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<uint32_t> size_dist(10, 1000);

    for (auto _ : state)
    {
        // Simulate market data updates
        for (int i = 0; i < 10; ++i)
        {
            int64_t mid_price = price_dist(rng);
            uint32_t size = size_dist(rng);

            handler.publish_quote(i % 5 + 1, // Rotate through 5 symbols
                                  mid_price - 5, mid_price + 5,
                                  size, size);
        }

        // Occasionally trigger volatility
        if (state.iterations() % 100 == 0)
        {
            handler.set_volatile_market(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            handler.set_volatile_market(false);
        }
    }

    handler.stop();

    state.SetItemsProcessed(state.iterations() * 10);
}

// Benchmark impact of latency difference on arbitrage
static void BM_LatencyArbitrageImpact(benchmark::State &state)
{
    network::ArbitrageDetector detector;
    std::atomic<uint64_t> profitable_opportunities{0};
    std::atomic<uint64_t> total_profit_bps{0};

    detector.set_callback([&](const network::ArbitrageOpportunity &opp)
                          {
        if (opp.is_profitable()) {
            profitable_opportunities.fetch_add(1);
            total_profit_bps.fetch_add(static_cast<uint64_t>(opp.profit_basis_points() * 100));
        } });

    const uint64_t latency_diff_ns = state.range(0) * 1000; // Convert μs to ns

    for (auto _ : state)
    {
        // Feed A quote (faster)
        network::Quote quote_a(1, 10000, 10010, 100, 100, 'A');
        detector.on_feed_update('A', network::MarketDataUpdate(quote_a));

        // Simulate latency difference
        benchmark::DoNotOptimize(latency_diff_ns);

        // Feed B quote (slower, with price movement)
        network::Quote quote_b(1, 10015, 10025, 100, 100, 'B');
        detector.on_feed_update('B', network::MarketDataUpdate(quote_b));
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["profit_opportunities_pct"] =
        static_cast<double>(profitable_opportunities.load()) / state.iterations() * 100;
    state.counters["avg_profit_bps"] =
        profitable_opportunities.load() > 0 ? static_cast<double>(total_profit_bps.load()) / profitable_opportunities.load() / 100 : 0;
}

BENCHMARK(BM_FeedLatencyNormal)->Iterations(1000);
BENCHMARK(BM_FeedLatencyVolatile)->Iterations(1000);
BENCHMARK(BM_ArbitrageDetection)->Iterations(10000);
BENCHMARK(BM_FullSystemWithFeeds)->Iterations(100);
BENCHMARK(BM_LatencyArbitrageImpact)->RangeMultiplier(10)->Range(1, 1000); // 1μs to 1ms

BENCHMARK_MAIN();