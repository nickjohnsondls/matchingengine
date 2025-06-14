#pragma once

#include <cstdint>
#include <chrono>

namespace micromatch::core
{

    // Order sides
    enum class Side : uint8_t
    {
        BUY = 0,
        SELL = 1
    };

    // Order types
    enum class OrderType : uint8_t
    {
        MARKET = 0,
        LIMIT = 1,
        STOP = 2,
        STOP_LIMIT = 3
    };

    // Order status
    enum class OrderStatus : uint8_t
    {
        NEW = 0,
        PARTIALLY_FILLED = 1,
        FILLED = 2,
        CANCELLED = 3,
        REJECTED = 4
    };

    // Time in force
    enum class TimeInForce : uint8_t
    {
        DAY = 0,
        GTC = 1, // Good Till Cancelled
        IOC = 2, // Immediate Or Cancel
        FOK = 3, // Fill Or Kill
        GTD = 4  // Good Till Date
    };

    // Cache-line aligned order structure (64 bytes)
    struct alignas(64) Order
    {
        // First 8 bytes
        uint64_t order_id;

        // Next 8 bytes
        uint64_t symbol_id;

        // Next 8 bytes - price in fixed-point (6 decimal places)
        // e.g., $123.456789 = 123456789
        int64_t price;

        // Next 8 bytes
        uint32_t quantity;
        uint32_t executed_quantity;

        // Next 8 bytes
        uint64_t timestamp_ns;

        // Next 8 bytes
        uint64_t client_id;

        // Next 8 bytes
        uint32_t sequence_number;
        Side side;
        OrderType type;
        OrderStatus status;
        TimeInForce tif;

        // Last 8 bytes - padding to reach 64 bytes
        uint8_t padding[8];

        // Default constructor
        Order() noexcept = default;

        // Constructor for limit orders
        Order(uint64_t id, uint64_t symbol, int64_t px, uint32_t qty,
              Side s, uint64_t client = 0) noexcept
            : order_id(id), symbol_id(symbol), price(px), quantity(qty), executed_quantity(0), timestamp_ns(std::chrono::steady_clock::now().time_since_epoch().count()), client_id(client), sequence_number(0), side(s), type(OrderType::LIMIT), status(OrderStatus::NEW), tif(TimeInForce::DAY), padding{} {}

        // Check if order is buy side
        [[nodiscard]] constexpr bool is_buy() const noexcept
        {
            return side == Side::BUY;
        }

        // Check if order is sell side
        [[nodiscard]] constexpr bool is_sell() const noexcept
        {
            return side == Side::SELL;
        }

        // Get remaining quantity
        [[nodiscard]] constexpr uint32_t remaining_quantity() const noexcept
        {
            return quantity - executed_quantity;
        }

        // Check if order is fully filled
        [[nodiscard]] constexpr bool is_filled() const noexcept
        {
            return executed_quantity >= quantity;
        }

        // Check if order can match with another order
        [[nodiscard]] bool can_match(const Order &other) const noexcept
        {
            // Same symbol check
            if (symbol_id != other.symbol_id)
                return false;

            // Can't match same side
            if (side == other.side)
                return false;

            // Price compatibility check
            if (is_buy())
            {
                // Buy order matches if its price >= sell price
                return price >= other.price;
            }
            else
            {
                // Sell order matches if its price <= buy price
                return price <= other.price;
            }
        }

        // Execute a partial fill
        void execute(uint32_t fill_quantity) noexcept
        {
            executed_quantity += fill_quantity;
            status = is_filled() ? OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;
        }
    };

    // Ensure our struct is exactly 64 bytes
    static_assert(sizeof(Order) == 64, "Order struct must be exactly 64 bytes");

    // Trade execution record
    struct alignas(64) Trade
    {
        uint64_t trade_id;
        uint64_t buy_order_id;
        uint64_t sell_order_id;
        uint64_t symbol_id;
        int64_t price;
        uint32_t quantity;
        uint32_t padding1;
        uint64_t timestamp_ns;
        uint64_t padding2;

        Trade(uint64_t id, const Order &buy, const Order &sell,
              int64_t exec_price, uint32_t qty) noexcept
            : trade_id(id), buy_order_id(buy.order_id), sell_order_id(sell.order_id), symbol_id(buy.symbol_id), price(exec_price), quantity(qty), padding1(0), timestamp_ns(std::chrono::steady_clock::now().time_since_epoch().count()), padding2(0) {}
    };

    // Ensure trade struct is exactly 64 bytes
    static_assert(sizeof(Trade) == 64, "Trade struct must be exactly 64 bytes");

} // namespace micromatch::core
