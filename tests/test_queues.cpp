#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <set>
#include <mutex>
#include <iostream>
#include <memory>
#include "utils/spsc_queue.hpp"
#include "utils/mpmc_queue.hpp"

using namespace micromatch::utils;

// Test fixture for SPSC Queue
class SPSCQueueTest : public ::testing::Test
{
protected:
    SPSCQueue<int> int_queue;
    SPSCQueue<std::string> string_queue;

    struct ComplexType
    {
        int id;
        std::string name;
        std::vector<double> data;

        ComplexType() = default;
        ComplexType(int i, const std::string &n)
            : id(i), name(n), data{1.0, 2.0, 3.0} {}
    };

    SPSCQueue<ComplexType> complex_queue;
};

// Basic SPSC Tests
TEST_F(SPSCQueueTest, BasicEnqueueDequeue)
{
    EXPECT_TRUE(int_queue.enqueue(42));
    auto result = int_queue.dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_F(SPSCQueueTest, EmptyQueue)
{
    EXPECT_TRUE(int_queue.empty());
    auto result = int_queue.dequeue();
    EXPECT_FALSE(result.has_value());
}

TEST_F(SPSCQueueTest, MultipleElements)
{
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(int_queue.enqueue(i));
    }

    for (int i = 0; i < 100; ++i)
    {
        auto result = int_queue.dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, i);
    }

    EXPECT_TRUE(int_queue.empty());
}

TEST_F(SPSCQueueTest, StringQueue)
{
    string_queue.enqueue("Hello");
    string_queue.enqueue("World");

    auto result1 = string_queue.dequeue();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(*result1, "Hello");

    auto result2 = string_queue.dequeue();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, "World");
}

TEST_F(SPSCQueueTest, ComplexType)
{
    ComplexType obj(1, "Test");
    complex_queue.enqueue(std::move(obj));

    auto result = complex_queue.dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, 1);
    EXPECT_EQ(result->name, "Test");
    EXPECT_EQ(result->data.size(), 3);
}

TEST_F(SPSCQueueTest, MoveSemantics)
{
    SPSCQueue<std::unique_ptr<int>> ptr_queue;

    auto ptr = std::make_unique<int>(42);
    ptr_queue.enqueue(std::move(ptr));

    auto result = ptr_queue.dequeue();
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->get(), nullptr);
    EXPECT_EQ(**result, 42);
}

// SPSC Concurrent Tests
TEST_F(SPSCQueueTest, ConcurrentProducerConsumer)
{
    const int num_items = 100000;
    std::atomic<bool> all_consumed{false};

    std::thread producer([&]()
                         {
        for (int i = 0; i < num_items; ++i) {
            int_queue.enqueue(i);
        } });

    std::thread consumer([&]()
                         {
        int expected = 0;
        while (expected < num_items) {
            auto result = int_queue.dequeue();
            if (result) {
                EXPECT_EQ(*result, expected);
                expected++;
            }
        }
        all_consumed = true; });

    producer.join();
    consumer.join();

    EXPECT_TRUE(all_consumed);
    EXPECT_TRUE(int_queue.empty());
}

TEST_F(SPSCQueueTest, StressTest)
{
    const int num_items = 1000000;

    std::thread producer([&]()
                         {
        for (int i = 0; i < num_items; ++i) {
            int_queue.enqueue(i);
            if (i % 1000 == 0) {
                std::this_thread::yield();
            }
        } });

    std::thread consumer([&]()
                         {
        long long sum = 0;  // Use long long to avoid overflow
        int count = 0;
        while (count < num_items) {
            auto result = int_queue.dequeue();
            if (result) {
                sum += *result;
                count++;
            }
        }
        
        // Verify sum using arithmetic series formula
        long long expected_sum = (long long)num_items * (num_items - 1) / 2;
        EXPECT_EQ(sum, expected_sum); });

    producer.join();
    consumer.join();
}

// Test fixture for MPMC Queue
class MPMCQueueTest : public ::testing::Test
{
protected:
    MPMCQueue<int, 1024> int_queue;
    MPMCQueue<std::string, 256> string_queue;
};

// Basic MPMC Tests
TEST_F(MPMCQueueTest, BasicEnqueueDequeue)
{
    EXPECT_TRUE(int_queue.try_enqueue(42));
    auto result = int_queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_F(MPMCQueueTest, EmptyQueue)
{
    EXPECT_TRUE(int_queue.empty());
    auto result = int_queue.try_dequeue();
    EXPECT_FALSE(result.has_value());
}

TEST_F(MPMCQueueTest, FullQueue)
{
    // Fill the queue
    for (size_t i = 0; i < int_queue.capacity(); ++i)
    {
        EXPECT_TRUE(int_queue.try_enqueue(i));
    }

    // Queue should be full
    EXPECT_FALSE(int_queue.try_enqueue(999));

    // Dequeue one item
    auto result = int_queue.try_dequeue();
    ASSERT_TRUE(result.has_value());

    // Now we should be able to enqueue again
    EXPECT_TRUE(int_queue.try_enqueue(999));
}

TEST_F(MPMCQueueTest, BlockingOperations)
{
    // Test blocking enqueue with timeout
    for (size_t i = 0; i < int_queue.capacity(); ++i)
    {
        EXPECT_TRUE(int_queue.enqueue(i, 100));
    }

    // Should fail after retries
    EXPECT_FALSE(int_queue.enqueue(999, 10));

    // Test blocking dequeue
    auto result = int_queue.dequeue(100);
    ASSERT_TRUE(result.has_value());
}

// MPMC Concurrent Tests
TEST_F(MPMCQueueTest, MultipleProducers)
{
    const int num_producers = 4;
    const int items_per_producer = 1000;
    std::atomic<int> total_consumed{0};

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i)
    {
        producers.emplace_back([&, i]()
                               {
            for (int j = 0; j < items_per_producer; ++j) {
                int value = i * items_per_producer + j;
                while (!int_queue.try_enqueue(value)) {
                    std::this_thread::yield();
                }
            } });
    }

    // Single consumer
    std::thread consumer([&]()
                         {
        std::set<int> seen;
        while (total_consumed < num_producers * items_per_producer) {
            auto result = int_queue.try_dequeue();
            if (result) {
                EXPECT_TRUE(seen.insert(*result).second); // No duplicates
                total_consumed++;
            }
        } });

    for (auto &t : producers)
    {
        t.join();
    }
    consumer.join();

    EXPECT_EQ(total_consumed, num_producers * items_per_producer);
}

TEST_F(MPMCQueueTest, MultipleConsumers)
{
    const int num_consumers = 4;
    const int total_items = 10000;
    std::atomic<int> total_consumed{0};

    // Single producer
    std::thread producer([&]()
                         {
        for (int i = 0; i < total_items; ++i) {
            while (!int_queue.try_enqueue(i)) {
                std::this_thread::yield();
            }
        } });

    std::vector<std::thread> consumers;
    std::mutex seen_mutex;
    std::set<int> seen;

    for (int i = 0; i < num_consumers; ++i)
    {
        consumers.emplace_back([&]()
                               {
            while (total_consumed < total_items) {
                auto result = int_queue.try_dequeue();
                if (result) {
                    {
                        std::lock_guard<std::mutex> lock(seen_mutex);
                        EXPECT_TRUE(seen.insert(*result).second); // No duplicates
                    }
                    total_consumed++;
                }
            } });
    }

    producer.join();
    for (auto &t : consumers)
    {
        t.join();
    }

    EXPECT_EQ(total_consumed, total_items);
}

TEST_F(MPMCQueueTest, MixedProducersConsumers)
{
    const int num_threads = 8;
    const int operations_per_thread = 1000;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&, i]()
                             {
            if (i % 2 == 0) {
                // Producer
                for (int j = 0; j < operations_per_thread; ++j) {
                    if (int_queue.try_enqueue(i * 1000 + j)) {
                        produced++;
                    }
                }
            } else {
                // Consumer
                for (int j = 0; j < operations_per_thread; ++j) {
                    if (int_queue.try_dequeue()) {
                        consumed++;
                    }
                }
            } });
    }

    for (auto &t : threads)
    {
        t.join();
    }

    // Drain remaining items
    while (int_queue.try_dequeue())
    {
        consumed++;
    }

    EXPECT_EQ(produced, consumed);
}

TEST_F(MPMCQueueTest, StressTest)
{
    const int num_threads = 16;
    const int duration_ms = 1000;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_ops{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&, i]()
                             {
            uint64_t local_ops = 0;
            while (!stop) {
                if (i % 2 == 0) {
                    if (int_queue.try_enqueue(i)) {
                        local_ops++;
                    }
                } else {
                    if (int_queue.try_dequeue()) {
                        local_ops++;
                    }
                }
            }
            total_ops += local_ops; });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    stop = true;

    for (auto &t : threads)
    {
        t.join();
    }

    std::cout << "Total operations: " << total_ops << std::endl;
    EXPECT_GT(total_ops, 0);
}

// Size and capacity tests
TEST_F(MPMCQueueTest, SizeApproximation)
{
    EXPECT_EQ(int_queue.size_approx(), 0);

    for (int i = 0; i < 10; ++i)
    {
        int_queue.try_enqueue(i);
    }

    EXPECT_GE(int_queue.size_approx(), 5); // Allow some approximation
    EXPECT_LE(int_queue.size_approx(), 10);
}

TEST_F(MPMCQueueTest, Capacity)
{
    EXPECT_EQ(int_queue.capacity(), 1024);
    EXPECT_EQ(string_queue.capacity(), 256);
}