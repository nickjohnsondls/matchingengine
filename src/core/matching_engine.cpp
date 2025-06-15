#include "core/matching_engine.hpp"
#include <thread>
#include <chrono>
#include <iostream>

namespace micromatch::core
{

    class MatchingEngineImpl : public IMatchingEngine
    {
    private:
        // Order books by symbol ID
        std::unordered_map<uint64_t, std::unique_ptr<IOrderBook>> order_books_;

        // Lock-free queue for order requests
        utils::SPSCQueue<OrderRequest> order_queue_;

        // Callbacks
        TradeCallback trade_callback_;
        OrderCallback order_callback_;

        // Statistics
        mutable MatchingEngineStats stats_;

        // Engine state
        std::atomic<bool> running_{false};
        std::thread worker_thread_;

        // Process a single order request
        void process_order_request(const OrderRequest &request)
        {
            switch (request.type)
            {
            case OrderRequest::NEW_ORDER:
                process_new_order(request.order);
                break;

            case OrderRequest::CANCEL_ORDER:
                process_cancel_order(request.symbol_id, request.order_id);
                break;

            case OrderRequest::MODIFY_ORDER:
                process_modify_order(request.symbol_id, request.order_id,
                                     request.new_price, request.new_quantity);
                break;
            }
        }

        // Process new order
        void process_new_order(const Order &order)
        {
            stats_.total_orders.fetch_add(1, std::memory_order_relaxed);

            // Find order book for symbol
            auto it = order_books_.find(order.symbol_id);
            if (it == order_books_.end())
            {
                // Symbol not registered
                stats_.rejected_orders.fetch_add(1, std::memory_order_relaxed);
                if (order_callback_)
                {
                    order_callback_(order, false);
                }
                return;
            }

            // Submit order to book
            auto &book = it->second;
            auto trades = book->add_order(order);

            // Notify order accepted
            if (order_callback_)
            {
                order_callback_(order, true);
            }

            // Process generated trades
            for (const auto &trade : trades)
            {
                stats_.total_trades.fetch_add(1, std::memory_order_relaxed);
                stats_.total_volume.fetch_add(trade.quantity, std::memory_order_relaxed);

                if (trade_callback_)
                {
                    trade_callback_(trade);
                }
            }
        }

        // Process cancel order
        void process_cancel_order(uint64_t symbol_id, uint64_t order_id)
        {
            auto it = order_books_.find(symbol_id);
            if (it == order_books_.end())
            {
                return;
            }

            auto &book = it->second;
            if (book->cancel_order(order_id))
            {
                stats_.cancelled_orders.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Process modify order
        void process_modify_order(uint64_t symbol_id, uint64_t order_id,
                                  int64_t new_price, uint32_t new_quantity)
        {
            auto it = order_books_.find(symbol_id);
            if (it == order_books_.end())
            {
                return;
            }

            auto &book = it->second;
            auto modified = book->modify_order(order_id, new_price, new_quantity);
            if (modified.has_value())
            {
                stats_.modified_orders.fetch_add(1, std::memory_order_relaxed);

                // Modification may generate trades (since it's cancel + new order)
                // Those are already handled by the order book
            }
        }

        // Worker thread function
        void worker_loop()
        {
            while (running_.load(std::memory_order_acquire))
            {
                auto request = order_queue_.dequeue();
                if (request.has_value())
                {
                    process_order_request(*request);
                }
                else
                {
                    // No orders, sleep briefly
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }

            // Process remaining orders before shutdown
            while (auto request = order_queue_.dequeue())
            {
                process_order_request(*request);
            }
        }

    public:
        MatchingEngineImpl() = default;

        ~MatchingEngineImpl()
        {
            if (running_)
            {
                stop();
            }
        }

        void submit_order(Order order) override
        {
            if (!running_)
            {
                throw std::runtime_error("Matching engine is not running");
            }

            auto request = OrderRequest::new_order(std::move(order));
            while (!order_queue_.enqueue(std::move(request)))
            {
                // Queue full, wait briefly
                std::this_thread::yield();
            }
        }

        void cancel_order(uint64_t symbol_id, uint64_t order_id) override
        {
            if (!running_)
            {
                throw std::runtime_error("Matching engine is not running");
            }

            auto request = OrderRequest::cancel_order(symbol_id, order_id);
            while (!order_queue_.enqueue(std::move(request)))
            {
                std::this_thread::yield();
            }
        }

        void modify_order(uint64_t symbol_id, uint64_t order_id,
                          int64_t new_price, uint32_t new_quantity) override
        {
            if (!running_)
            {
                throw std::runtime_error("Matching engine is not running");
            }

            auto request = OrderRequest::modify_order(symbol_id, order_id, new_price, new_quantity);
            while (!order_queue_.enqueue(std::move(request)))
            {
                std::this_thread::yield();
            }
        }

        bool register_symbol(uint64_t symbol_id) override
        {
            if (order_books_.find(symbol_id) != order_books_.end())
            {
                return false; // Already registered
            }

            order_books_[symbol_id] = create_order_book(symbol_id);
            return true;
        }

        bool unregister_symbol(uint64_t symbol_id) override
        {
            auto it = order_books_.find(symbol_id);
            if (it == order_books_.end())
            {
                return false; // Not found
            }

            // Clear the book first
            it->second->clear();
            order_books_.erase(it);
            return true;
        }

        IOrderBook *get_order_book(uint64_t symbol_id) override
        {
            auto it = order_books_.find(symbol_id);
            return (it != order_books_.end()) ? it->second.get() : nullptr;
        }

        void set_trade_callback(TradeCallback callback) override
        {
            trade_callback_ = std::move(callback);
        }

        void set_order_callback(OrderCallback callback) override
        {
            order_callback_ = std::move(callback);
        }

        MatchingEngineStats get_stats() const override
        {
            return stats_;
        }

        void clear_all_books() override
        {
            for (auto &[symbol_id, book] : order_books_)
            {
                book->clear();
            }
        }

        void start() override
        {
            if (running_.exchange(true))
            {
                throw std::runtime_error("Matching engine already running");
            }

            worker_thread_ = std::thread(&MatchingEngineImpl::worker_loop, this);
        }

        void stop() override
        {
            if (!running_.exchange(false))
            {
                return; // Already stopped
            }

            if (worker_thread_.joinable())
            {
                worker_thread_.join();
            }
        }

        bool is_running() const override
        {
            return running_.load(std::memory_order_acquire);
        }
    };

    // Factory function implementation
    std::unique_ptr<IMatchingEngine> create_matching_engine()
    {
        return std::make_unique<MatchingEngineImpl>();
    }

} // namespace micromatch::core