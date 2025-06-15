#pragma once

#include "orderbook.hpp"
#include "utils/spsc_queue.hpp"
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>

namespace micromatch::core
{

    // Forward declarations
    class MatchingEngineImpl;

    // Callback types for trade and order events
    using TradeCallback = std::function<void(const Trade &)>;
    using OrderCallback = std::function<void(const Order &, bool accepted)>;

    // Matching engine statistics
    struct MatchingEngineStats
    {
        std::atomic<uint64_t> total_orders{0};
        std::atomic<uint64_t> total_trades{0};
        std::atomic<uint64_t> total_volume{0};
        std::atomic<uint64_t> rejected_orders{0};
        std::atomic<uint64_t> cancelled_orders{0};
        std::atomic<uint64_t> modified_orders{0};

        void reset()
        {
            total_orders = 0;
            total_trades = 0;
            total_volume = 0;
            rejected_orders = 0;
            cancelled_orders = 0;
            modified_orders = 0;
        }
    };

    // Simple stats structure for returning values
    struct MatchingEngineStatsSnapshot
    {
        uint64_t total_orders;
        uint64_t total_trades;
        uint64_t total_volume;
        uint64_t rejected_orders;
        uint64_t cancelled_orders;
        uint64_t modified_orders;
    };

    // Matching engine interface
    class IMatchingEngine
    {
    public:
        virtual ~IMatchingEngine() = default;

        // Submit a new order
        virtual void submit_order(Order order) = 0;

        // Cancel an existing order
        virtual void cancel_order(uint64_t symbol_id, uint64_t order_id) = 0;

        // Modify an existing order
        virtual void modify_order(uint64_t symbol_id, uint64_t order_id,
                                  int64_t new_price, uint32_t new_quantity) = 0;

        // Register symbol for trading
        virtual bool register_symbol(uint64_t symbol_id) = 0;

        // Unregister symbol
        virtual bool unregister_symbol(uint64_t symbol_id) = 0;

        // Get order book for a symbol
        virtual IOrderBook *get_order_book(uint64_t symbol_id) = 0;

        // Set callbacks
        virtual void set_trade_callback(TradeCallback callback) = 0;
        virtual void set_order_callback(OrderCallback callback) = 0;

        // Get statistics
        virtual MatchingEngineStatsSnapshot get_stats() const = 0;

        // Clear all order books
        virtual void clear_all_books() = 0;

        // Start/stop the engine
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual bool is_running() const = 0;
    };

    // Factory function to create a matching engine
    [[nodiscard]] std::unique_ptr<IMatchingEngine> create_matching_engine();

    // Order request types for internal processing
    struct OrderRequest
    {
        enum Type
        {
            NEW_ORDER,
            CANCEL_ORDER,
            MODIFY_ORDER
        };

        Type type;
        Order order;
        uint64_t order_id;
        uint64_t symbol_id;
        int64_t new_price;
        uint32_t new_quantity;

        // Constructors for different request types
        static OrderRequest new_order(Order order)
        {
            OrderRequest req;
            req.type = NEW_ORDER;
            req.order = std::move(order);
            return req;
        }

        static OrderRequest cancel_order(uint64_t symbol_id, uint64_t order_id)
        {
            OrderRequest req;
            req.type = CANCEL_ORDER;
            req.symbol_id = symbol_id;
            req.order_id = order_id;
            return req;
        }

        static OrderRequest modify_order(uint64_t symbol_id, uint64_t order_id,
                                         int64_t new_price, uint32_t new_quantity)
        {
            OrderRequest req;
            req.type = MODIFY_ORDER;
            req.symbol_id = symbol_id;
            req.order_id = order_id;
            req.new_price = new_price;
            req.new_quantity = new_quantity;
            return req;
        }
    };

} // namespace micromatch::core