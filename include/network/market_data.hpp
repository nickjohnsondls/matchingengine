#pragma once

#include <cstdint>
#include <chrono>
#include <string>

namespace micromatch::network
{

    // Market data update types
    enum class UpdateType : uint8_t
    {
        QUOTE = 0,
        TRADE = 1,
        IMBALANCE = 2,
        STATUS = 3
    };

    // Level 1 quote data
    struct Quote
    {
        uint64_t symbol_id;
        int64_t bid_price;
        int64_t ask_price;
        uint32_t bid_size;
        uint32_t ask_size;
        uint64_t timestamp_ns;
        uint64_t sequence_number;
        char feed_id; // 'A' or 'B'

        Quote() = default;
        Quote(uint64_t sym, int64_t bid, int64_t ask, uint32_t bid_sz, uint32_t ask_sz, char feed)
            : symbol_id(sym), bid_price(bid), ask_price(ask),
              bid_size(bid_sz), ask_size(ask_sz),
              timestamp_ns(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
              sequence_number(0), feed_id(feed) {}
    };

    // Trade data
    struct TradeTick
    {
        uint64_t symbol_id;
        int64_t price;
        uint32_t quantity;
        uint64_t timestamp_ns;
        uint64_t sequence_number;
        char feed_id;
        bool is_buy_side;

        TradeTick() = default;
        TradeTick(uint64_t sym, int64_t px, uint32_t qty, char feed, bool buy)
            : symbol_id(sym), price(px), quantity(qty),
              timestamp_ns(std::chrono::high_resolution_clock::now().time_since_epoch().count()),
              sequence_number(0), feed_id(feed), is_buy_side(buy) {}
    };

    // Market data update wrapper
    struct MarketDataUpdate
    {
        UpdateType type;
        union
        {
            Quote quote;
            TradeTick trade;
        };

        MarketDataUpdate() : type(UpdateType::QUOTE), quote() {}
        explicit MarketDataUpdate(const Quote &q) : type(UpdateType::QUOTE), quote(q) {}
        explicit MarketDataUpdate(const TradeTick &t) : type(UpdateType::TRADE), trade(t) {}
    };

    // Feed statistics for monitoring
    struct FeedStats
    {
        uint64_t messages_received{0};
        uint64_t messages_dropped{0};
        uint64_t latency_sum_ns{0};
        uint64_t latency_min_ns{UINT64_MAX};
        uint64_t latency_max_ns{0};
        uint64_t jitter_events{0}; // Count of high jitter occurrences
        uint64_t last_sequence{0};
        std::chrono::steady_clock::time_point last_update;

        void update_latency(uint64_t latency_ns)
        {
            latency_sum_ns += latency_ns;
            latency_min_ns = std::min(latency_min_ns, latency_ns);
            latency_max_ns = std::max(latency_max_ns, latency_ns);
            messages_received++;
        }

        double average_latency_us() const
        {
            return messages_received > 0 ? static_cast<double>(latency_sum_ns) / messages_received / 1000.0 : 0.0;
        }
    };

} // namespace micromatch::network