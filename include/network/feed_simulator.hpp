#pragma once

#include "market_data.hpp"
#include "utils/spsc_queue.hpp"
#include <atomic>
#include <random>
#include <thread>
#include <functional>
#include <memory>

namespace micromatch::network
{

    // Configuration for feed behavior
    struct FeedConfig
    {
        uint64_t base_latency_ns = 5000;   // 5μs base latency
        uint64_t jitter_normal_ns = 1000;  // 1μs normal jitter
        uint64_t jitter_spike_ns = 500000; // 500μs spike jitter
        double spike_probability = 0.001;  // 0.1% chance of spike
        double drop_probability = 0.0001;  // 0.01% packet drop
        bool is_primary_feed = true;       // Primary feeds are typically faster
        uint64_t sequence_start = 1;

        // Volatility settings
        bool volatile_market = false;
        uint64_t volatile_jitter_multiplier = 100; // 100x jitter during volatility
    };

    // Feed simulator that injects realistic latency patterns
    class FeedSimulator
    {
    public:
        using MessageCallback = std::function<void(const MarketDataUpdate &, const FeedStats &)>;

        FeedSimulator(char feed_id, const FeedConfig &config = FeedConfig())
            : feed_id_(feed_id), config_(config), running_(false), sequence_number_(config.sequence_start), rng_(std::random_device{}()), latency_dist_(0.0, 1.0), spike_dist_(0.0, 1.0), drop_dist_(0.0, 1.0) {}

        ~FeedSimulator()
        {
            stop();
        }

        // Start the feed simulation
        void start()
        {
            if (running_.exchange(true))
            {
                return;
            }

            worker_thread_ = std::thread(&FeedSimulator::worker_loop, this);
        }

        // Stop the feed
        void stop()
        {
            if (!running_.exchange(false))
            {
                return;
            }

            if (worker_thread_.joinable())
            {
                worker_thread_.join();
            }
        }

        // Submit market data to the feed
        void publish_quote(uint64_t symbol_id, int64_t bid, int64_t ask,
                           uint32_t bid_size, uint32_t ask_size)
        {
            Quote quote(symbol_id, bid, ask, bid_size, ask_size, feed_id_);
            quote.sequence_number = sequence_number_.fetch_add(1);

            MarketDataUpdate update(quote);
            pending_updates_.enqueue(std::move(update));
        }

        void publish_trade(uint64_t symbol_id, int64_t price, uint32_t quantity, bool is_buy)
        {
            TradeTick trade(symbol_id, price, quantity, feed_id_, is_buy);
            trade.sequence_number = sequence_number_.fetch_add(1);

            MarketDataUpdate update(trade);
            pending_updates_.enqueue(std::move(update));
        }

        // Set callback for processed messages
        void set_callback(MessageCallback callback)
        {
            callback_ = std::move(callback);
        }

        // Control market volatility
        void set_volatile_market(bool volatile_market)
        {
            config_.volatile_market = volatile_market;
        }

        // Get current stats
        FeedStats get_stats() const
        {
            return stats_;
        }

        char get_feed_id() const { return feed_id_; }

    private:
        void worker_loop()
        {
            while (running_.load(std::memory_order_acquire))
            {
                auto update_opt = pending_updates_.dequeue();
                if (!update_opt)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                    continue;
                }

                // Simulate network latency and jitter
                inject_latency();

                // Simulate packet drops
                if (should_drop_packet())
                {
                    stats_.messages_dropped++;
                    continue;
                }

                // Record stats
                auto now = std::chrono::steady_clock::now();
                if (stats_.last_update.time_since_epoch().count() > 0)
                {
                    auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          now - stats_.last_update)
                                          .count();
                    stats_.update_latency(latency_ns);

                    // Detect jitter events (latency > 10x average)
                    if (stats_.messages_received > 100 &&
                        latency_ns > stats_.average_latency_us() * 10000)
                    {
                        stats_.jitter_events++;
                    }
                }
                stats_.last_update = now;

                // Deliver the update
                if (callback_)
                {
                    callback_(*update_opt, stats_);
                }
            }
        }

        void inject_latency()
        {
            uint64_t latency_ns = config_.base_latency_ns;

            // Add jitter
            if (config_.volatile_market)
            {
                // During volatility, jitter increases dramatically
                uint64_t jitter = config_.jitter_normal_ns * config_.volatile_jitter_multiplier;
                latency_ns += static_cast<uint64_t>(latency_dist_(rng_) * jitter);
            }
            else
            {
                // Normal market conditions
                if (spike_dist_(rng_) < config_.spike_probability)
                {
                    // Occasional spike
                    latency_ns += config_.jitter_spike_ns;
                }
                else
                {
                    // Normal jitter
                    latency_ns += static_cast<uint64_t>(latency_dist_(rng_) * config_.jitter_normal_ns);
                }
            }

            // Secondary feeds typically have additional latency
            if (!config_.is_primary_feed)
            {
                latency_ns += 500000; // Additional 500μs for backup feed
            }

            std::this_thread::sleep_for(std::chrono::nanoseconds(latency_ns));
        }

        bool should_drop_packet()
        {
            return drop_dist_(rng_) < config_.drop_probability;
        }

        char feed_id_;
        FeedConfig config_;
        std::atomic<bool> running_;
        std::atomic<uint64_t> sequence_number_;

        utils::SPSCQueue<MarketDataUpdate> pending_updates_;
        MessageCallback callback_;
        FeedStats stats_;

        std::thread worker_thread_;

        // Random number generation
        std::mt19937 rng_;
        std::uniform_real_distribution<double> latency_dist_;
        std::uniform_real_distribution<double> spike_dist_;
        std::uniform_real_distribution<double> drop_dist_;
    };

} // namespace micromatch::network