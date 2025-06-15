#pragma once

#include <atomic>
#include <memory>
#include <new>
#include <optional>
#include <cstddef>
#include <cstring>

namespace micromatch::utils
{

    /**
     * Lock-free Single Producer Single Consumer (SPSC) Queue
     *
     * High-performance queue optimized for single producer/consumer scenarios.
     * Uses cache-line padding to prevent false sharing.
     *
     * @tparam T Type of elements stored in the queue
     */
    template <typename T>
    class SPSCQueue
    {
    private:
        static constexpr size_t CACHE_LINE_SIZE = 64;

        struct Node
        {
            std::atomic<Node *> next{nullptr};
            T data;

            template <typename... Args>
            Node(Args &&...args) : data(std::forward<Args>(args)...) {}
        };

        // Padding to prevent false sharing between producer and consumer
        alignas(CACHE_LINE_SIZE) std::atomic<Node *> head_;
        alignas(CACHE_LINE_SIZE) std::atomic<Node *> tail_;
        alignas(CACHE_LINE_SIZE) Node *cached_head_; // Producer's cached head
        alignas(CACHE_LINE_SIZE) Node *cached_tail_; // Consumer's cached tail

    public:
        SPSCQueue()
        {
            Node *dummy = new Node();
            head_.store(dummy, std::memory_order_relaxed);
            tail_.store(dummy, std::memory_order_relaxed);
            cached_head_ = dummy;
            cached_tail_ = dummy;
        }

        ~SPSCQueue()
        {
            // Clean up remaining nodes
            while (Node *node = head_.load(std::memory_order_relaxed))
            {
                Node *next = node->next.load(std::memory_order_relaxed);
                delete node;
                head_.store(next, std::memory_order_relaxed);
            }
        }

        // Delete copy operations
        SPSCQueue(const SPSCQueue &) = delete;
        SPSCQueue &operator=(const SPSCQueue &) = delete;

        // Move operations
        SPSCQueue(SPSCQueue &&other) noexcept
        {
            head_.store(other.head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            tail_.store(other.tail_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            cached_head_ = other.cached_head_;
            cached_tail_ = other.cached_tail_;

            // Reset other
            Node *dummy = new Node();
            other.head_.store(dummy, std::memory_order_relaxed);
            other.tail_.store(dummy, std::memory_order_relaxed);
            other.cached_head_ = dummy;
            other.cached_tail_ = dummy;
        }

        /**
         * Enqueue an item (producer only)
         * @param value Item to enqueue
         * @return true if successful
         */
        template <typename U>
        bool enqueue(U &&value)
        {
            Node *new_node = new Node(std::forward<U>(value));

            // Link new node
            cached_tail_->next.store(new_node, std::memory_order_release);
            cached_tail_ = new_node;

            // Update tail for consumer visibility
            tail_.store(new_node, std::memory_order_release);

            return true;
        }

        /**
         * Try to dequeue an item (consumer only)
         * @return Optional containing the dequeued item if successful
         */
        std::optional<T> dequeue()
        {
            Node *head = cached_head_;
            Node *next = head->next.load(std::memory_order_acquire);

            if (next == nullptr)
            {
                // Queue might be empty, check the actual head
                head = head_.load(std::memory_order_acquire);
                if (head == cached_head_)
                {
                    return std::nullopt; // Queue is empty
                }
                cached_head_ = head;
                next = head->next.load(std::memory_order_acquire);
                if (next == nullptr)
                {
                    return std::nullopt;
                }
            }

            // Read data before updating head
            T data = std::move(next->data);

            // Update head
            head_.store(next, std::memory_order_release);
            cached_head_ = next;

            // Delete old dummy node
            delete head;

            return data;
        }

        /**
         * Check if queue is empty (consumer only)
         * @return true if empty
         */
        bool empty() const
        {
            Node *head = head_.load(std::memory_order_acquire);
            return head->next.load(std::memory_order_acquire) == nullptr;
        }

        /**
         * Get approximate size (not exact in concurrent environment)
         * @return Approximate number of elements
         */
        size_t size_approx() const
        {
            size_t count = 0;
            Node *current = head_.load(std::memory_order_acquire);
            while (current)
            {
                current = current->next.load(std::memory_order_acquire);
                if (current)
                    count++;
            }
            return count;
        }
    };

} // namespace micromatch::utils