#pragma once

#include "order.hpp"
#include <vector>
#include <memory>
#include <optional>

namespace micromatch::core
{

    // Forward declarations
    class OrderBookImpl;

    // Order book interface
    class IOrderBook
    {
    public:
        virtual ~IOrderBook() = default;

        // Add a new order to the book
        // Returns trades if the order matches existing orders
        [[nodiscard]] virtual std::vector<Trade> add_order(Order order) = 0;

        // Cancel an existing order
        // Returns true if order was found and cancelled
        [[nodiscard]] virtual bool cancel_order(uint64_t order_id) = 0;

        // Modify an existing order (price and/or quantity)
        // This is typically implemented as cancel + add new
        [[nodiscard]] virtual std::optional<Order> modify_order(
            uint64_t order_id,
            int64_t new_price,
            uint32_t new_quantity) = 0;

        // Get current best bid price (highest buy price)
        [[nodiscard]] virtual std::optional<int64_t> best_bid() const = 0;

        // Get current best ask price (lowest sell price)
        [[nodiscard]] virtual std::optional<int64_t> best_ask() const = 0;

        // Get total volume at a price level
        [[nodiscard]] virtual uint32_t volume_at_price(int64_t price, Side side) const = 0;

        // Get number of orders at a price level
        [[nodiscard]] virtual uint32_t order_count_at_price(int64_t price, Side side) const = 0;

        // Get the symbol this book is for
        [[nodiscard]] virtual uint64_t symbol_id() const = 0;

        // Get total number of orders in the book
        [[nodiscard]] virtual size_t total_orders() const = 0;

        // Clear all orders (typically at end of day)
        virtual void clear() = 0;
    };

    // Factory function to create an order book
    [[nodiscard]] std::unique_ptr<IOrderBook> create_order_book(uint64_t symbol_id);

    // Market data snapshot
    struct MarketDataSnapshot
    {
        uint64_t symbol_id;
        std::optional<int64_t> best_bid;
        std::optional<int64_t> best_ask;
        uint32_t bid_volume; // Volume at best bid
        uint32_t ask_volume; // Volume at best ask
        uint32_t bid_orders; // Number of orders at best bid
        uint32_t ask_orders; // Number of orders at best ask
        uint64_t timestamp_ns;

        MarketDataSnapshot(const IOrderBook &book) noexcept
            : symbol_id(book.symbol_id()), best_bid(book.best_bid()), best_ask(book.best_ask()), bid_volume(best_bid ? book.volume_at_price(*best_bid, Side::BUY) : 0), ask_volume(best_ask ? book.volume_at_price(*best_ask, Side::SELL) : 0), bid_orders(best_bid ? book.order_count_at_price(*best_bid, Side::BUY) : 0), ask_orders(best_ask ? book.order_count_at_price(*best_ask, Side::SELL) : 0), timestamp_ns(std::chrono::steady_clock::now().time_since_epoch().count()) {}
    };

    // Price level information
    struct PriceLevel
    {
        int64_t price;
        uint32_t total_volume;
        uint32_t order_count;

        PriceLevel(int64_t p, uint32_t v, uint32_t c) noexcept
            : price(p), total_volume(v), order_count(c) {}
    };

    // Order book depth (top N levels)
    struct OrderBookDepth
    {
        static constexpr size_t MAX_DEPTH = 10;

        uint64_t symbol_id;
        std::vector<PriceLevel> bids; // Sorted highest to lowest
        std::vector<PriceLevel> asks; // Sorted lowest to highest
        uint64_t timestamp_ns;

        OrderBookDepth(uint64_t sym) noexcept
            : symbol_id(sym), timestamp_ns(std::chrono::steady_clock::now().time_since_epoch().count())
        {
            bids.reserve(MAX_DEPTH);
            asks.reserve(MAX_DEPTH);
        }
    };

} // namespace micromatch::core