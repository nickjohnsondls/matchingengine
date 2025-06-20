#pragma once

#include "feed_simulator.hpp"
#include "arbitrage_detector.hpp"
#include "core/matching_engine.hpp"
#include <memory>
#include <iostream>
#include <iomanip>

namespace micromatch::network
{

    // Main feed handler that manages A/B feeds and arbitrage detection
    class FeedHandler
    {
    public:
        FeedHandler(std::unique_ptr<core::IMatchingEngine> matching_engine)
            : matching_engine_(std::move(matching_engine)), arbitrage_detector_(std::make_unique<ArbitrageDetector>())
        {

            // Configure Feed A (primary, faster)
            FeedConfig config_a;
            config_a.is_primary_feed = true;
            config_a.base_latency_ns = 5000;    // 5μs base
            config_a.jitter_normal_ns = 1000;   // 1μs jitter
            config_a.jitter_spike_ns = 500000;  // 500μs spikes
            config_a.spike_probability = 0.001; // 0.1% spike chance

            // Configure Feed B (backup, slower)
            FeedConfig config_b;
            config_b.is_primary_feed = false;
            config_b.base_latency_ns = 10000;   // 10μs base
            config_b.jitter_normal_ns = 2000;   // 2μs jitter
            config_b.jitter_spike_ns = 1000000; // 1ms spikes
            config_b.spike_probability = 0.002; // 0.2% spike chance

            feed_a_ = std::make_unique<FeedSimulator>('A', config_a);
            feed_b_ = std::make_unique<FeedSimulator>('B', config_b);

            setup_callbacks();
        }

        // Start both feeds
        void start()
        {
            feed_a_->start();
            feed_b_->start();
            std::cout << "Feed handler started with A/B feeds" << std::endl;
        }

        // Stop feeds
        void stop()
        {
            feed_a_->stop();
            feed_b_->stop();
            std::cout << "Feed handler stopped" << std::endl;
        }

        // Publish market data to both feeds
        void publish_quote(uint64_t symbol_id, int64_t bid, int64_t ask,
                           uint32_t bid_size, uint32_t ask_size)
        {
            feed_a_->publish_quote(symbol_id, bid, ask, bid_size, ask_size);
            feed_b_->publish_quote(symbol_id, bid, ask, bid_size, ask_size);
        }

        void publish_trade(uint64_t symbol_id, int64_t price, uint32_t quantity, bool is_buy)
        {
            feed_a_->publish_trade(symbol_id, price, quantity, is_buy);
            feed_b_->publish_trade(symbol_id, price, quantity, is_buy);
        }

        // Control market volatility
        void set_volatile_market(bool is_volatile)
        {
            feed_a_->set_volatile_market(is_volatile);
            feed_b_->set_volatile_market(is_volatile);

            if (is_volatile)
            {
                std::cout << "MARKET VOLATILITY: Jitter increased 100x!" << std::endl;
            }
            else
            {
                std::cout << "Market conditions: Normal" << std::endl;
            }
        }

        // Get feed statistics
        void print_stats() const
        {
            auto stats_a = feed_a_->get_stats();
            auto stats_b = feed_b_->get_stats();
            auto arb_stats = arbitrage_detector_->get_stats();

            std::cout << "\n=== Feed Statistics ===" << std::endl;
            std::cout << "Feed A:" << std::endl;
            std::cout << "  Messages: " << stats_a.messages_received
                      << " (dropped: " << stats_a.messages_dropped << ")" << std::endl;
            std::cout << "  Avg latency: " << std::fixed << std::setprecision(2)
                      << stats_a.average_latency_us() << " μs" << std::endl;
            std::cout << "  Jitter events: " << stats_a.jitter_events << std::endl;

            std::cout << "\nFeed B:" << std::endl;
            std::cout << "  Messages: " << stats_b.messages_received
                      << " (dropped: " << stats_b.messages_dropped << ")" << std::endl;
            std::cout << "  Avg latency: " << std::fixed << std::setprecision(2)
                      << stats_b.average_latency_us() << " μs" << std::endl;
            std::cout << "  Jitter events: " << stats_b.jitter_events << std::endl;

            std::cout << "\n=== Arbitrage Detection ===" << std::endl;
            std::cout << "Opportunities detected: " << arb_stats.opportunities_detected << std::endl;
            std::cout << "Profitable opportunities: " << arb_stats.profitable_opportunities << std::endl;
            std::cout << "Missed opportunities: " << arb_stats.missed_opportunities << std::endl;
            std::cout << "Average profit: " << std::fixed << std::setprecision(2)
                      << arb_stats.average_profit_bps() << " bps" << std::endl;
            std::cout << "Average latency diff: " << std::fixed << std::setprecision(2)
                      << arb_stats.average_latency_diff_us() << " μs" << std::endl;
            std::cout << "Max latency diff: " << std::fixed << std::setprecision(2)
                      << arb_stats.max_latency_diff_ns / 1000.0 << " μs" << std::endl;
        }

        // Get recent arbitrage opportunities
        std::vector<ArbitrageOpportunity> get_recent_arbitrage(size_t count = 10) const
        {
            return arbitrage_detector_->get_recent_opportunities(count);
        }

        // Direct access to arbitrage detector for testing
        ArbitrageDetector *get_arbitrage_detector() { return arbitrage_detector_.get(); }

        // Get matching engine stats
        core::MatchingEngineStatsSnapshot get_engine_stats() const
        {
            return matching_engine_->get_stats();
        }

    private:
        void setup_callbacks()
        {
            // Feed A callback
            feed_a_->set_callback([this](const MarketDataUpdate &update, const FeedStats &stats)
                                  { process_feed_update('A', update, stats); });

            // Feed B callback
            feed_b_->set_callback([this](const MarketDataUpdate &update, const FeedStats &stats)
                                  { process_feed_update('B', update, stats); });

            // Arbitrage detection callback
            arbitrage_detector_->set_callback([this](const ArbitrageOpportunity &opp)
                                              { on_arbitrage_detected(opp); });
        }

        void process_feed_update(char feed_id, const MarketDataUpdate &update, const FeedStats &stats)
        {
            // Send to arbitrage detector
            arbitrage_detector_->on_feed_update(feed_id, update);

            // Convert to order for matching engine (using feed A as primary)
            if (feed_id == 'A' && update.type == UpdateType::QUOTE)
            {
                // In a real system, this would be more sophisticated
                // For demo, we'll create market maker orders from quotes
                const auto &quote = update.quote;

                // Cancel previous quotes for this symbol
                // (In production, you'd track order IDs)

                // Place new bid/ask orders
                if (quote.bid_price > 0 && quote.bid_size > 0)
                {
                    core::Order bid_order;
                    bid_order.order_id = generate_order_id();
                    bid_order.symbol_id = quote.symbol_id;
                    bid_order.side = core::Side::BUY;
                    bid_order.price = quote.bid_price;
                    bid_order.quantity = quote.bid_size;

                    matching_engine_->submit_order(std::move(bid_order));
                }

                if (quote.ask_price > 0 && quote.ask_size > 0)
                {
                    core::Order ask_order;
                    ask_order.order_id = generate_order_id();
                    ask_order.symbol_id = quote.symbol_id;
                    ask_order.side = core::Side::SELL;
                    ask_order.price = quote.ask_price;
                    ask_order.quantity = quote.ask_size;

                    matching_engine_->submit_order(std::move(ask_order));
                }
            }
        }

        void on_arbitrage_detected(const ArbitrageOpportunity &opp)
        {
            if (opp.is_profitable() && opp.profit_basis_points() > 1.0)
            { // Only log significant opportunities
                std::cout << "[ARBITRAGE] Symbol " << opp.symbol_id
                          << ": " << std::fixed << std::setprecision(2)
                          << opp.profit_basis_points() << " bps profit, "
                          << "latency diff: " << opp.latency_difference_ns / 1000.0 << " μs, "
                          << "fast feed: " << opp.fast_feed << std::endl;
            }
        }

        uint64_t generate_order_id()
        {
            static std::atomic<uint64_t> next_id{1000000}; // Start at 1M to distinguish from user orders
            return next_id.fetch_add(1);
        }

        std::unique_ptr<core::IMatchingEngine> matching_engine_;
        std::unique_ptr<FeedSimulator> feed_a_;
        std::unique_ptr<FeedSimulator> feed_b_;
        std::unique_ptr<ArbitrageDetector> arbitrage_detector_;
    };

} // namespace micromatch::network