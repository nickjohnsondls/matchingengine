#include "core/orderbook.hpp"
#include <map>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <cassert>
#include <iostream>

namespace micromatch::core
{

    // Price level containing orders at a specific price
    class PriceLevelImpl
    {
    private:
        int64_t price_;
        std::queue<std::shared_ptr<Order>> orders_;
        uint32_t total_volume_{0};

    public:
        explicit PriceLevelImpl(int64_t price) : price_(price) {}

        void add_order(std::shared_ptr<Order> order)
        {
            assert(order->price == price_);
            orders_.push(order);
            total_volume_ += order->quantity;
        }

        std::shared_ptr<Order> peek_front() const
        {
            return orders_.empty() ? nullptr : orders_.front();
        }

        void remove_front()
        {
            if (!orders_.empty())
            {
                total_volume_ -= orders_.front()->quantity;
                orders_.pop();
            }
        }

        void remove_front_after_fill(uint32_t filled_quantity)
        {
            if (!orders_.empty())
            {
                // The order has already been filled, so we subtract the filled quantity
                total_volume_ -= filled_quantity;
                orders_.pop();
            }
        }

        bool remove_order(uint64_t order_id)
        {
            std::queue<std::shared_ptr<Order>> temp_queue;
            bool found = false;

            while (!orders_.empty())
            {
                auto order = orders_.front();
                orders_.pop();

                if (order->order_id == order_id)
                {
                    total_volume_ -= order->quantity;
                    found = true;
                }
                else
                {
                    temp_queue.push(order);
                }
            }

            orders_ = std::move(temp_queue);
            return found;
        }

        void update_front_quantity(uint32_t new_quantity)
        {
            if (!orders_.empty())
            {
                auto &front_order = orders_.front();
                // Update total volume: subtract old quantity, add new quantity
                total_volume_ = total_volume_ - front_order->quantity + new_quantity;
                // Note: Don't update front_order->quantity here as it's already been updated
            }
        }

        void update_volume_after_partial_fill(uint32_t filled_quantity)
        {
            // Simply subtract the filled quantity from total volume
            total_volume_ -= filled_quantity;
        }

        bool empty() const { return orders_.empty(); }
        size_t order_count() const { return orders_.size(); }
        uint32_t volume() const { return total_volume_; }
        int64_t price() const { return price_; }
    };

    // OrderBook implementation
    class OrderBookImpl : public IOrderBook
    {
    private:
        uint64_t symbol_id_;

        // Buy orders: price -> level (sorted high to low)
        std::map<int64_t, std::unique_ptr<PriceLevelImpl>, std::greater<int64_t>> buy_levels_;

        // Sell orders: price -> level (sorted low to high)
        std::map<int64_t, std::unique_ptr<PriceLevelImpl>, std::less<int64_t>> sell_levels_;

        // Order ID -> Order mapping for fast lookup
        std::unordered_map<uint64_t, std::shared_ptr<Order>> order_map_;

        // Trade ID generator
        uint64_t next_trade_id_{1};

        // Helper to generate trade
        Trade generate_trade(const Order &aggressive_order, const Order &passive_order,
                             uint32_t quantity, int64_t price)
        {
            return Trade(next_trade_id_++, aggressive_order, passive_order, price, quantity);
        }

        // Match a buy order against sell orders
        std::vector<Trade> match_buy_order(std::shared_ptr<Order> buy_order)
        {
            std::vector<Trade> trades;

            while (buy_order->quantity > 0 && !sell_levels_.empty())
            {
                auto &[best_ask_price, best_ask_level] = *sell_levels_.begin();

                // Check if buy price crosses the spread
                if (buy_order->price < best_ask_price)
                {
                    break; // No match possible
                }

                auto sell_order = best_ask_level->peek_front();
                if (!sell_order)
                {
                    sell_levels_.erase(sell_levels_.begin());
                    continue;
                }

                // Calculate match quantity
                uint32_t match_quantity = std::min(buy_order->quantity, sell_order->quantity);

                // Generate trade at passive order price (price-time priority)
                trades.push_back(generate_trade(*buy_order, *sell_order, match_quantity, best_ask_price));

                // Update quantities
                buy_order->quantity -= match_quantity;
                sell_order->quantity -= match_quantity;

                if (sell_order->quantity == 0)
                {
                    // Remove fully filled sell order
                    order_map_.erase(sell_order->order_id);
                    best_ask_level->remove_front_after_fill(match_quantity);

                    if (best_ask_level->empty())
                    {
                        sell_levels_.erase(sell_levels_.begin());
                    }
                }
                else
                {
                    // Update partially filled sell order
                    best_ask_level->update_volume_after_partial_fill(match_quantity);
                }
            }

            return trades;
        }

        // Match a sell order against buy orders
        std::vector<Trade> match_sell_order(std::shared_ptr<Order> sell_order)
        {
            std::vector<Trade> trades;

            while (sell_order->quantity > 0 && !buy_levels_.empty())
            {
                auto &[best_bid_price, best_bid_level] = *buy_levels_.begin();

                // Check if sell price crosses the spread
                if (sell_order->price > best_bid_price)
                {
                    break; // No match possible
                }

                auto buy_order = best_bid_level->peek_front();
                if (!buy_order)
                {
                    buy_levels_.erase(buy_levels_.begin());
                    continue;
                }

                // Calculate match quantity
                uint32_t match_quantity = std::min(sell_order->quantity, buy_order->quantity);

                // Generate trade at passive order price (price-time priority)
                trades.push_back(generate_trade(*sell_order, *buy_order, match_quantity, best_bid_price));

                // Update quantities
                sell_order->quantity -= match_quantity;
                buy_order->quantity -= match_quantity;

                if (buy_order->quantity == 0)
                {
                    // Remove fully filled buy order
                    order_map_.erase(buy_order->order_id);
                    best_bid_level->remove_front_after_fill(match_quantity);

                    if (best_bid_level->empty())
                    {
                        buy_levels_.erase(buy_levels_.begin());
                    }
                }
                else
                {
                    // Update partially filled buy order
                    best_bid_level->update_volume_after_partial_fill(match_quantity);
                }
            }

            return trades;
        }

        // Add order to the appropriate level
        void add_to_book(std::shared_ptr<Order> order)
        {
            if (order->side == Side::BUY)
            {
                auto &level = buy_levels_[order->price];
                if (!level)
                {
                    level = std::make_unique<PriceLevelImpl>(order->price);
                }
                level->add_order(order);
            }
            else
            {
                auto &level = sell_levels_[order->price];
                if (!level)
                {
                    level = std::make_unique<PriceLevelImpl>(order->price);
                }
                level->add_order(order);
            }

            order_map_[order->order_id] = order;
        }

    public:
        explicit OrderBookImpl(uint64_t symbol_id) : symbol_id_(symbol_id) {}

        std::vector<Trade> add_order(Order order) override
        {
            // Validate order
            if (order.quantity == 0 || order.price <= 0)
            {
                return {}; // Invalid order
            }

            // Create shared pointer for the order
            auto order_ptr = std::make_shared<Order>(std::move(order));

            // Check for duplicate order ID
            if (order_map_.find(order_ptr->order_id) != order_map_.end())
            {
                return {}; // Duplicate order ID
            }

            // Match the order
            std::vector<Trade> trades;
            if (order_ptr->side == Side::BUY)
            {
                trades = match_buy_order(order_ptr);
            }
            else
            {
                trades = match_sell_order(order_ptr);
            }

            // Add remaining quantity to book
            if (order_ptr->quantity > 0)
            {
                add_to_book(order_ptr);
            }

            return trades;
        }

        bool cancel_order(uint64_t order_id) override
        {
            auto it = order_map_.find(order_id);
            if (it == order_map_.end())
            {
                return false; // Order not found
            }

            auto order = it->second;
            order_map_.erase(it);

            // Remove from price level
            if (order->side == Side::BUY)
            {
                auto level_it = buy_levels_.find(order->price);
                if (level_it != buy_levels_.end())
                {
                    level_it->second->remove_order(order_id);
                    if (level_it->second->empty())
                    {
                        buy_levels_.erase(level_it);
                    }
                }
            }
            else
            {
                auto level_it = sell_levels_.find(order->price);
                if (level_it != sell_levels_.end())
                {
                    level_it->second->remove_order(order_id);
                    if (level_it->second->empty())
                    {
                        sell_levels_.erase(level_it);
                    }
                }
            }

            return true;
        }

        std::optional<Order> modify_order(uint64_t order_id, int64_t new_price,
                                          uint32_t new_quantity) override
        {
            auto it = order_map_.find(order_id);
            if (it == order_map_.end())
            {
                return std::nullopt; // Order not found
            }

            auto old_order = *it->second;

            // Cancel the old order
            if (!cancel_order(order_id))
            {
                return std::nullopt;
            }

            // Create new order with same ID but new price/quantity
            Order new_order = old_order;
            new_order.price = new_price;
            new_order.quantity = new_quantity;
            new_order.timestamp_ns = std::chrono::steady_clock::now().time_since_epoch().count();

            // Add the modified order (this will also match it)
            add_order(new_order);

            return new_order;
        }

        std::optional<int64_t> best_bid() const override
        {
            if (buy_levels_.empty())
            {
                return std::nullopt;
            }
            return buy_levels_.begin()->first;
        }

        std::optional<int64_t> best_ask() const override
        {
            if (sell_levels_.empty())
            {
                return std::nullopt;
            }
            return sell_levels_.begin()->first;
        }

        uint32_t volume_at_price(int64_t price, Side side) const override
        {
            if (side == Side::BUY)
            {
                auto it = buy_levels_.find(price);
                return (it != buy_levels_.end()) ? it->second->volume() : 0;
            }
            else
            {
                auto it = sell_levels_.find(price);
                return (it != sell_levels_.end()) ? it->second->volume() : 0;
            }
        }

        uint32_t order_count_at_price(int64_t price, Side side) const override
        {
            if (side == Side::BUY)
            {
                auto it = buy_levels_.find(price);
                return (it != buy_levels_.end()) ? it->second->order_count() : 0;
            }
            else
            {
                auto it = sell_levels_.find(price);
                return (it != sell_levels_.end()) ? it->second->order_count() : 0;
            }
        }

        uint64_t symbol_id() const override
        {
            return symbol_id_;
        }

        size_t total_orders() const override
        {
            return order_map_.size();
        }

        void clear() override
        {
            buy_levels_.clear();
            sell_levels_.clear();
            order_map_.clear();
        }
    };

    // Factory function implementation
    std::unique_ptr<IOrderBook> create_order_book(uint64_t symbol_id)
    {
        return std::make_unique<OrderBookImpl>(symbol_id);
    }

} // namespace micromatch::core