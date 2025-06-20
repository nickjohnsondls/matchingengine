#pragma once

#include "market_data.hpp"
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <vector>
#include <algorithm>

namespace micromatch::network
{

    // Arbitrage opportunity detected between feeds
    struct ArbitrageOpportunity
    {
        uint64_t symbol_id;
        char fast_feed;
        char slow_feed;
        int64_t price_difference;
        uint64_t latency_difference_ns;
        uint64_t timestamp_ns;

        // For quotes
        int64_t feed_a_bid;
        int64_t feed_a_ask;
        int64_t feed_b_bid;
        int64_t feed_b_ask;

        // Potential profit calculation (in basis points)
        double profit_basis_points() const
        {
            if (feed_a_ask > 0 && feed_b_bid > 0 && feed_b_bid > feed_a_ask)
            {
                // Can buy on A and sell on B
                return ((double)(feed_b_bid - feed_a_ask) / feed_a_ask) * 10000;
            }
            else if (feed_b_ask > 0 && feed_a_bid > 0 && feed_a_bid > feed_b_ask)
            {
                // Can buy on B and sell on A
                return ((double)(feed_a_bid - feed_b_ask) / feed_b_ask) * 10000;
            }
            return 0.0;
        }

        bool is_profitable() const
        {
            return profit_basis_points() > 0.0;
        }
    };

    // Statistics about arbitrage detection
    struct ArbitrageStats
    {
        uint64_t opportunities_detected{0};
        uint64_t profitable_opportunities{0};
        uint64_t missed_opportunities{0}; // Due to latency
        double total_profit_bps{0.0};
        uint64_t max_latency_diff_ns{0};
        uint64_t total_latency_diff_ns{0};

        void record_opportunity(const ArbitrageOpportunity &opp)
        {
            opportunities_detected++;
            if (opp.is_profitable())
            {
                profitable_opportunities++;
                total_profit_bps += opp.profit_basis_points();
            }
            max_latency_diff_ns = std::max(max_latency_diff_ns, opp.latency_difference_ns);
            total_latency_diff_ns += opp.latency_difference_ns;
        }

        double average_latency_diff_us() const
        {
            return opportunities_detected > 0 ? static_cast<double>(total_latency_diff_ns) / opportunities_detected / 1000.0 : 0.0;
        }

        double average_profit_bps() const
        {
            return profitable_opportunities > 0 ? total_profit_bps / profitable_opportunities : 0.0;
        }
    };

    // Detects arbitrage opportunities between A/B feeds
    class ArbitrageDetector
    {
    public:
        using ArbitrageCallback = std::function<void(const ArbitrageOpportunity &)>;

        ArbitrageDetector() = default;

        // Process updates from feeds
        void on_feed_update(char feed_id, const MarketDataUpdate &update)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (update.type == UpdateType::QUOTE)
            {
                process_quote(feed_id, update.quote);
            }
            else if (update.type == UpdateType::TRADE)
            {
                process_trade(feed_id, update.trade);
            }
        }

        // Set callback for detected opportunities
        void set_callback(ArbitrageCallback callback)
        {
            callback_ = std::move(callback);
        }

        // Get current stats
        ArbitrageStats get_stats() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return stats_;
        }

        // Get recent opportunities
        std::vector<ArbitrageOpportunity> get_recent_opportunities(size_t count = 10) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t start = recent_opportunities_.size() > count ? recent_opportunities_.size() - count : 0;
            return std::vector<ArbitrageOpportunity>(
                recent_opportunities_.begin() + start,
                recent_opportunities_.end());
        }

    private:
        struct SymbolState
        {
            Quote feed_a_quote;
            Quote feed_b_quote;
            bool has_feed_a{false};
            bool has_feed_b{false};

            void update_quote(char feed_id, const Quote &quote)
            {
                if (feed_id == 'A')
                {
                    feed_a_quote = quote;
                    has_feed_a = true;
                }
                else
                {
                    feed_b_quote = quote;
                    has_feed_b = true;
                }
            }

            bool has_both_feeds() const
            {
                return has_feed_a && has_feed_b;
            }
        };

        void process_quote(char feed_id, const Quote &quote)
        {
            auto &state = symbol_states_[quote.symbol_id];
            state.update_quote(feed_id, quote);

            if (state.has_both_feeds())
            {
                check_arbitrage(quote.symbol_id, state);
            }
        }

        void process_trade(char feed_id, const TradeTick &trade)
        {
            // Track trade disparities between feeds
            // This could indicate one feed is faster for trade reporting
            auto &trade_times = trade_timestamps_[trade.symbol_id];
            trade_times[feed_id] = trade.timestamp_ns;

            if (trade_times.count('A') > 0 && trade_times.count('B') > 0)
            {
                uint64_t diff = std::abs(static_cast<int64_t>(trade_times['A'] - trade_times['B']));
                if (diff > 1000000)
                { // More than 1ms difference
                    stats_.missed_opportunities++;
                }
            }
        }

        void check_arbitrage(uint64_t symbol_id, const SymbolState &state)
        {
            // Check for price discrepancies
            bool has_opportunity = false;

            // Cross-market arbitrage: Can we buy on one feed and sell on another?
            if (state.feed_a_quote.ask_price > 0 && state.feed_b_quote.bid_price > 0 &&
                state.feed_b_quote.bid_price > state.feed_a_quote.ask_price)
            {
                has_opportunity = true;
            }
            else if (state.feed_b_quote.ask_price > 0 && state.feed_a_quote.bid_price > 0 &&
                     state.feed_a_quote.bid_price > state.feed_b_quote.ask_price)
            {
                has_opportunity = true;
            }

            // Also check for quote disparities (different prices on same side)
            int64_t bid_diff = std::abs(state.feed_a_quote.bid_price - state.feed_b_quote.bid_price);
            int64_t ask_diff = std::abs(state.feed_a_quote.ask_price - state.feed_b_quote.ask_price);

            if (bid_diff > 0 || ask_diff > 0 || has_opportunity)
            {
                ArbitrageOpportunity opp;
                opp.symbol_id = symbol_id;
                opp.timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();

                // Determine which feed is faster
                if (state.feed_a_quote.timestamp_ns < state.feed_b_quote.timestamp_ns)
                {
                    opp.fast_feed = 'A';
                    opp.slow_feed = 'B';
                    opp.latency_difference_ns = state.feed_b_quote.timestamp_ns - state.feed_a_quote.timestamp_ns;
                }
                else
                {
                    opp.fast_feed = 'B';
                    opp.slow_feed = 'A';
                    opp.latency_difference_ns = state.feed_a_quote.timestamp_ns - state.feed_b_quote.timestamp_ns;
                }

                opp.feed_a_bid = state.feed_a_quote.bid_price;
                opp.feed_a_ask = state.feed_a_quote.ask_price;
                opp.feed_b_bid = state.feed_b_quote.bid_price;
                opp.feed_b_ask = state.feed_b_quote.ask_price;
                opp.price_difference = std::max(bid_diff, ask_diff);

                stats_.record_opportunity(opp);
                recent_opportunities_.push_back(opp);

                // Keep only last 1000 opportunities
                if (recent_opportunities_.size() > 1000)
                {
                    recent_opportunities_.erase(recent_opportunities_.begin());
                }

                if (callback_)
                {
                    callback_(opp);
                }
            }
        }

        mutable std::mutex mutex_;
        std::unordered_map<uint64_t, SymbolState> symbol_states_;
        std::unordered_map<uint64_t, std::unordered_map<char, uint64_t>> trade_timestamps_;
        std::vector<ArbitrageOpportunity> recent_opportunities_;
        ArbitrageStats stats_;
        ArbitrageCallback callback_;
    };

} // namespace micromatch::network