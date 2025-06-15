#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include "utils/spsc_queue.hpp"
#include "utils/mpmc_queue.hpp"

using namespace micromatch::utils;

// Test data structure
struct TestData
{
    uint64_t id;
    uint64_t timestamp;
    double value;
    char padding[40]; // Make it cache-line sized

    TestData() : id(0), timestamp(0), value(0.0) {}
    TestData(uint64_t i) : id(i), timestamp(i), value(i * 1.5) {}
};

// SPSC Queue Benchmarks
static void BM_SPSC_Enqueue(benchmark::State &state)
{
    SPSCQueue<TestData> queue;
    uint64_t counter = 0;

    for (auto _ : state)
    {
        TestData data(counter++);
        benchmark::DoNotOptimize(queue.enqueue(std::move(data)));
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SPSC_Enqueue);

static void BM_SPSC_Dequeue(benchmark::State &state)
{
    SPSCQueue<TestData> queue;

    // Pre-fill queue
    for (int i = 0; i < 10000; ++i)
    {
        queue.enqueue(TestData(i));
    }

    for (auto _ : state)
    {
        auto result = queue.dequeue();
        benchmark::DoNotOptimize(result);
        if (!result)
        {
            state.SkipWithError("Queue empty");
            break;
        }
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SPSC_Dequeue);

static void BM_SPSC_PingPong(benchmark::State &state)
{
    SPSCQueue<TestData> queue1;
    SPSCQueue<TestData> queue2;
    std::atomic<bool> stop{false};

    auto producer = std::thread([&]()
                                {
        uint64_t counter = 0;
        while (!stop.load(std::memory_order_acquire)) {
            queue1.enqueue(TestData(counter++));
            
            // Wait for response
            while (!stop.load(std::memory_order_acquire)) {
                auto result = queue2.dequeue();
                if (result) break;
            }
        } });

    for (auto _ : state)
    {
        // Consumer receives and responds
        auto result = queue1.dequeue();
        if (result)
        {
            queue2.enqueue(std::move(*result));
        }
    }

    stop.store(true, std::memory_order_release);
    producer.join();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SPSC_PingPong);

// MPMC Queue Benchmarks
static void BM_MPMC_SingleProducer(benchmark::State &state)
{
    MPMCQueue<TestData, 1024> queue;
    uint64_t counter = 0;

    for (auto _ : state)
    {
        TestData data(counter++);
        bool success = queue.try_enqueue(std::move(data));
        benchmark::DoNotOptimize(success);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MPMC_SingleProducer);

static void BM_MPMC_SingleConsumer(benchmark::State &state)
{
    MPMCQueue<TestData, 1024> queue;

    // Pre-fill queue
    for (int i = 0; i < 512; ++i)
    {
        queue.try_enqueue(TestData(i));
    }

    for (auto _ : state)
    {
        auto result = queue.try_dequeue();
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MPMC_SingleConsumer);

static void BM_MPMC_MultiProducer(benchmark::State &state)
{
    const int num_producers = state.range(0);
    MPMCQueue<TestData, 8192> queue;
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_produced{0};

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i)
    {
        producers.emplace_back([&, i]()
                               {
            uint64_t local_counter = i * 1000000;
            uint64_t produced = 0;
            
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            while (!stop.load(std::memory_order_acquire)) {
                if (queue.try_enqueue(TestData(local_counter++))) {
                    produced++;
                }
            }
            
            total_produced.fetch_add(produced, std::memory_order_relaxed); });
    }

    start.store(true, std::memory_order_release);

    for (auto _ : state)
    {
        auto result = queue.try_dequeue();
        benchmark::DoNotOptimize(result);
    }

    stop.store(true, std::memory_order_release);
    for (auto &t : producers)
    {
        t.join();
    }

    state.SetItemsProcessed(total_produced.load());
}
BENCHMARK(BM_MPMC_MultiProducer)->Range(1, 8);

static void BM_MPMC_MultiConsumer(benchmark::State &state)
{
    const int num_consumers = state.range(0);
    MPMCQueue<TestData, 8192> queue;
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_consumed{0};

    // Producer thread
    std::thread producer([&]()
                         {
        uint64_t counter = 0;
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        
        while (!stop.load(std::memory_order_acquire)) {
            queue.try_enqueue(TestData(counter++));
        } });

    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; ++i)
    {
        consumers.emplace_back([&]()
                               {
            uint64_t consumed = 0;
            
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            while (!stop.load(std::memory_order_acquire)) {
                if (queue.try_dequeue()) {
                    consumed++;
                }
            }
            
            total_consumed.fetch_add(consumed, std::memory_order_relaxed); });
    }

    start.store(true, std::memory_order_release);

    for (auto _ : state)
    {
        // Main thread acts as additional producer
        queue.try_enqueue(TestData(0));
    }

    stop.store(true, std::memory_order_release);
    producer.join();
    for (auto &t : consumers)
    {
        t.join();
    }

    state.SetItemsProcessed(total_consumed.load());
}
BENCHMARK(BM_MPMC_MultiConsumer)->Range(1, 8);

// Latency benchmarks
static void BM_SPSC_Latency(benchmark::State &state)
{
    SPSCQueue<TestData> queue;

    for (auto _ : state)
    {
        auto start = std::chrono::high_resolution_clock::now();

        TestData data(42);
        queue.enqueue(std::move(data));
        auto result = queue.dequeue();

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }

    state.SetLabel("Round-trip latency");
}
BENCHMARK(BM_SPSC_Latency)->UseManualTime();

static void BM_MPMC_Latency(benchmark::State &state)
{
    MPMCQueue<TestData, 1024> queue;

    for (auto _ : state)
    {
        auto start = std::chrono::high_resolution_clock::now();

        TestData data(42);
        queue.try_enqueue(std::move(data));
        auto result = queue.try_dequeue();

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }

    state.SetLabel("Round-trip latency");
}
BENCHMARK(BM_MPMC_Latency)->UseManualTime();

// Contention benchmarks
static void BM_MPMC_Contention(benchmark::State &state)
{
    const int num_threads = state.range(0);
    MPMCQueue<TestData, 16384> queue;
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_ops{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&, i]()
                             {
            std::mt19937 rng(i);
            std::uniform_int_distribution<int> dist(0, 1);
            uint64_t local_ops = 0;
            uint64_t counter = i * 1000000;
            
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            while (!stop.load(std::memory_order_acquire)) {
                if (dist(rng) == 0) {
                    // Producer
                    if (queue.try_enqueue(TestData(counter++))) {
                        local_ops++;
                    }
                } else {
                    // Consumer
                    if (queue.try_dequeue()) {
                        local_ops++;
                    }
                }
            }
            
            total_ops.fetch_add(local_ops, std::memory_order_relaxed); });
    }

    start.store(true, std::memory_order_release);

    for (auto _ : state)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stop.store(true, std::memory_order_release);
    for (auto &t : threads)
    {
        t.join();
    }

    state.SetItemsProcessed(total_ops.load());
    state.SetLabel("Mixed producers/consumers");
}
BENCHMARK(BM_MPMC_Contention)->Range(2, 16)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();