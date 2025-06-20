#include "network/feed_handler.hpp"
#include "core/matching_engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <iomanip>
#include <csignal>

using namespace micromatch;

std::atomic<bool> running{true};

void signal_handler(int signal)
{
    std::cout << "\nShutting down..." << std::endl;
    running = false;
}

void print_arbitrage_opportunity(const network::ArbitrageOpportunity &opp)
{
    std::cout << "\n[ARBITRAGE ALERT]" << std::endl;
    std::cout << "Symbol: " << opp.symbol_id << std::endl;
    std::cout << "Profit: " << std::fixed << std::setprecision(2)
              << opp.profit_basis_points() << " basis points" << std::endl;
    std::cout << "Fast Feed: " << opp.fast_feed << ", Slow Feed: " << opp.slow_feed << std::endl;
    std::cout << "Latency Difference: " << std::fixed << std::setprecision(2)
              << opp.latency_difference_ns / 1000.0 << " μs" << std::endl;
    std::cout << "Feed A (Bid/Ask): " << opp.feed_a_bid / 100.0 << "/" << opp.feed_a_ask / 100.0 << std::endl;
    std::cout << "Feed B (Bid/Ask): " << opp.feed_b_bid / 100.0 << "/" << opp.feed_b_ask / 100.0 << std::endl;
}

void generate_market_data(network::FeedHandler &handler)
{
    std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int64_t> price_movement(-50, 50); // -0.50 to +0.50
    std::uniform_int_distribution<uint32_t> size_dist(100, 1000);
    std::uniform_real_distribution<double> volatility_chance(0.0, 1.0);

    // Starting prices for 5 symbols (in cents)
    std::vector<int64_t> mid_prices = {10000, 5000, 15000, 8000, 12000}; // $100, $50, $150, $80, $120

    int tick = 0;

    while (running.load())
    {
        tick++;

        // 5% chance to trigger market volatility every 10 ticks
        if (tick % 10 == 0 && volatility_chance(rng) < 0.05)
        {
            std::cout << "\n*** MARKET VOLATILITY EVENT ***" << std::endl;
            handler.set_volatile_market(true);

            // Volatile period lasts 2-5 seconds
            std::this_thread::sleep_for(std::chrono::seconds(2 + rng() % 4));

            handler.set_volatile_market(false);
            std::cout << "*** Volatility subsided ***" << std::endl;
        }

        // Update quotes for each symbol
        for (size_t i = 0; i < mid_prices.size(); ++i)
        {
            // Random walk the mid price
            mid_prices[i] += price_movement(rng);

            // Ensure price stays positive
            if (mid_prices[i] < 100)
                mid_prices[i] = 100;

            // Generate bid/ask spread (tighter for liquid symbols)
            int64_t spread = (i == 0) ? 1 : (2 + i); // Symbol 0 has 1 cent spread, others wider

            int64_t bid = mid_prices[i] - spread / 2;
            int64_t ask = mid_prices[i] + spread / 2;

            uint32_t bid_size = size_dist(rng);
            uint32_t ask_size = size_dist(rng);

            handler.publish_quote(i + 1, bid, ask, bid_size, ask_size);
        }

        // Occasionally publish trades
        if (tick % 5 == 0)
        {
            uint64_t symbol = (rng() % 5) + 1;
            bool is_buy = rng() % 2;
            int64_t price = is_buy ? mid_prices[symbol - 1] + 1 : mid_prices[symbol - 1] - 1;
            uint32_t quantity = size_dist(rng) / 10; // Smaller trade sizes

            handler.publish_trade(symbol, price, quantity, is_buy);
        }

        // Market data update rate: 100 updates/second
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main()
{
    std::signal(SIGINT, signal_handler);

    std::cout << "=== MicroMatch Network Layer Demo ===" << std::endl;
    std::cout << "Demonstrating A/B feed arbitrage detection and jitter impact\n"
              << std::endl;

    // Create matching engine
    auto matching_engine = core::create_matching_engine();
    matching_engine->start();

    // Create feed handler with A/B feeds
    network::FeedHandler handler(std::move(matching_engine));

    // Set up arbitrage alert callback
    handler.get_arbitrage_detector()->set_callback(print_arbitrage_opportunity);

    // Start feeds
    handler.start();

    std::cout << "Feed handler started. Generating market data..." << std::endl;
    std::cout << "Press Ctrl+C to stop\n"
              << std::endl;

    // Start market data generation in separate thread
    std::thread market_data_thread(generate_market_data, std::ref(handler));

    // Print statistics periodically
    auto last_stats_time = std::chrono::steady_clock::now();

    while (running.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time);

        if (elapsed.count() >= 10)
        { // Print stats every 10 seconds
            handler.print_stats();

            // Show matching engine stats too
            auto engine_stats = handler.get_engine_stats();
            std::cout << "\n=== Matching Engine Stats ===" << std::endl;
            std::cout << "Orders processed: " << engine_stats.total_orders << std::endl;
            std::cout << "Trades executed: " << engine_stats.total_trades << std::endl;

            last_stats_time = now;
        }
    }

    // Cleanup
    market_data_thread.join();
    handler.stop();

    std::cout << "\nFinal Statistics:" << std::endl;
    handler.print_stats();

    return 0;
}