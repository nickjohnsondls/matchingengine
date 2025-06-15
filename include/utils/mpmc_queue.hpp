#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <cstddef>
#include <array>
#include <thread>

namespace micromatch::utils
{

    /**
     * Lock-free Multiple Producer Multiple Consumer (MPMC) Queue
     *
     * Bounded queue suitable for monitoring, logging, and multi-threaded scenarios.
     * Uses a ring buffer with atomic indices for lock-free operation.
     *
     * @tparam T Type of elements stored in the queue
     * @tparam Size Maximum number of elements (must be power of 2)
     */
    template <typename T, size_t Size>
    class MPMCQueue
    {
        static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

    private:
        static constexpr size_t CACHE_LINE_SIZE = 64;

        struct Cell
        {
            std::atomic<size_t> sequence;
            T data;

            Cell() : sequence(0) {}
        };

        alignas(CACHE_LINE_SIZE) std::array<Cell, Size> buffer_;
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> enqueue_pos_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> dequeue_pos_{0};

        static constexpr size_t MASK = Size - 1;

    public:
        MPMCQueue()
        {
            for (size_t i = 0; i < Size; ++i)
            {
                buffer_[i].sequence.store(i, std::memory_order_relaxed);
            }
        }

        ~MPMCQueue() = default;

        // Delete copy operations
        MPMCQueue(const MPMCQueue &) = delete;
        MPMCQueue &operator=(const MPMCQueue &) = delete;

        /**
         * Try to enqueue an item
         * @param value Item to enqueue
         * @return true if successful, false if queue is full
         */
        template <typename U>
        bool try_enqueue(U &&value)
        {
            size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

            for (;;)
            {
                Cell &cell = buffer_[pos & MASK];
                size_t seq = cell.sequence.load(std::memory_order_acquire);
                intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

                if (diff == 0)
                {
                    // Cell is ready for writing
                    if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                                           std::memory_order_relaxed))
                    {
                        // We got the slot
                        cell.data = std::forward<U>(value);
                        cell.sequence.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (diff < 0)
                {
                    // Queue is full
                    return false;
                }
                else
                {
                    // Another thread is ahead, retry
                    pos = enqueue_pos_.load(std::memory_order_relaxed);
                }
            }
        }

        /**
         * Try to dequeue an item
         * @return Optional containing the dequeued item if successful
         */
        std::optional<T> try_dequeue()
        {
            size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

            for (;;)
            {
                Cell &cell = buffer_[pos & MASK];
                size_t seq = cell.sequence.load(std::memory_order_acquire);
                intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

                if (diff == 0)
                {
                    // Cell has data ready
                    if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                                           std::memory_order_relaxed))
                    {
                        // We got the data
                        T data = std::move(cell.data);
                        cell.sequence.store(pos + Size, std::memory_order_release);
                        return data;
                    }
                }
                else if (diff < 0)
                {
                    // Queue is empty
                    return std::nullopt;
                }
                else
                {
                    // Another thread is ahead, retry
                    pos = dequeue_pos_.load(std::memory_order_relaxed);
                }
            }
        }

        /**
         * Blocking enqueue with timeout
         * @param value Item to enqueue
         * @param max_retries Maximum number of retries before giving up
         * @return true if successful
         */
        template <typename U>
        bool enqueue(U &&value, size_t max_retries = 1000)
        {
            for (size_t i = 0; i < max_retries; ++i)
            {
                if (try_enqueue(std::forward<U>(value)))
                {
                    return true;
                }
                // Exponential backoff
                if (i < 10)
                {
                    __builtin_ia32_pause(); // CPU pause instruction
                }
                else
                {
                    std::this_thread::yield();
                }
            }
            return false;
        }

        /**
         * Blocking dequeue with timeout
         * @param max_retries Maximum number of retries before giving up
         * @return Optional containing the dequeued item if successful
         */
        std::optional<T> dequeue(size_t max_retries = 1000)
        {
            for (size_t i = 0; i < max_retries; ++i)
            {
                auto result = try_dequeue();
                if (result.has_value())
                {
                    return result;
                }
                // Exponential backoff
                if (i < 10)
                {
                    __builtin_ia32_pause(); // CPU pause instruction
                }
                else
                {
                    std::this_thread::yield();
                }
            }
            return std::nullopt;
        }

        /**
         * Check if queue is empty (approximate)
         * @return true if empty
         */
        bool empty() const
        {
            size_t enq = enqueue_pos_.load(std::memory_order_acquire);
            size_t deq = dequeue_pos_.load(std::memory_order_acquire);
            return enq == deq;
        }

        /**
         * Get approximate size
         * @return Approximate number of elements
         */
        size_t size_approx() const
        {
            size_t enq = enqueue_pos_.load(std::memory_order_acquire);
            size_t deq = dequeue_pos_.load(std::memory_order_acquire);
            return (enq - deq) & MASK;
        }

        /**
         * Get maximum capacity
         * @return Maximum number of elements
         */
        static constexpr size_t capacity()
        {
            return Size;
        }
    };

} // namespace micromatch::utils