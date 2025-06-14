# Tech Stack Details - MicroMatch

## Core Language & Build Tools

### C++20

- **Why**: Zero-cost abstractions, deterministic performance, no garbage
  collection
- **Features Used**: Concepts, coroutines, ranges, atomic operations
- **Alternative Considered**: Rust (but C++ has more mature HFT ecosystem)

### CMake 3.20+

- **Why**: Industry standard, excellent IDE support, cross-platform
- **Features Used**: FetchContent, precompiled headers, unity builds
- **Alternative Considered**: Bazel (but CMake has better C++ ecosystem support)

### Conan 2.0

- **Why**: Modern C++ package manager, binary caching, easy dependency
  management
- **Features Used**: Binary packages, profiles for different architectures
- **Alternative Considered**: vcpkg (but Conan has better binary management)

## High-Performance Components

### Intel TBB (Threading Building Blocks)

- **Why**: Industry-proven parallel algorithms, scalable memory allocators
- **Features Used**: concurrent_hash_map, scalable_allocator, parallel_for
- **Use Case**: Parallel order processing across symbols

### Boost.Intrusive

- **Why**: Zero allocation containers, cache-friendly layouts
- **Features Used**: intrusive::rbtree for price levels, intrusive::list for
  orders
- **Use Case**: Order book implementation without heap allocations

### SPSC/MPMC Queues

- **Why**: Lock-free communication between threads
- **Implementation**: Custom implementation based on Dmitry Vyukov's algorithms
- **Use Case**: Order flow from gateway to matching engine

## Networking & Messaging

### ASIO (Standalone)

- **Why**: Header-only, high-performance async I/O, no Boost dependency
- **Features Used**: io_context, steady_timer, UDP/TCP sockets
- **Use Case**: Network communication for order entry and market data

### ZeroMQ

- **Why**: Battle-tested messaging patterns, built-in reliability
- **Features Used**: PUB/SUB for market data, PUSH/PULL for orders
- **Use Case**: Scalable market data distribution

### Protocol Buffers

- **Why**: Efficient serialization, schema evolution, language agnostic
- **Features Used**: Arena allocation, zero-copy deserialization
- **Use Case**: Order and market data message formats

## Monitoring & Observability

### Prometheus C++ Client

- **Why**: Industry standard metrics, efficient histogram implementation
- **Features Used**: Counter, Gauge, Histogram with custom buckets
- **Use Case**: Latency percentiles, throughput metrics

### spdlog

- **Why**: Fast, header-only, async logging
- **Features Used**: Ring buffer async logger, custom formatters
- **Use Case**: Debug logging without impacting latency

### Grafana

- **Why**: Real-time dashboards, Prometheus integration
- **Features Used**: Heatmaps for latency, time series for throughput
- **Use Case**: Visual monitoring of system performance

## Testing & Quality

### Google Test

- **Why**: Mature, feature-rich, good IDE integration
- **Features Used**: Parameterized tests, death tests, custom matchers
- **Use Case**: Unit testing order book logic

### Google Benchmark

- **Why**: Statistical rigor, CPU cycle measurements
- **Features Used**: Custom counters, complexity analysis
- **Use Case**: Measuring order processing latency

### Valgrind/Perf

- **Why**: Industry standard profiling tools
- **Features Used**: Cachegrind for cache analysis, perf stat for CPU counters
- **Use Case**: Identifying performance bottlenecks

## Infrastructure

### Docker

- **Why**: Reproducible builds, easy deployment
- **Features Used**: Multi-stage builds, minimal Alpine images
- **Use Case**: Consistent development and testing environment

### docker-compose

- **Why**: Multi-container orchestration, service dependencies
- **Features Used**: Health checks, volume mounts, network isolation
- **Use Case**: Running full system with monitoring stack

## Performance Optimizations

### Memory Management

- **tcmalloc/jemalloc**: Alternative allocators for better multi-threaded
  performance
- **Huge Pages**: 2MB pages for reduced TLB misses
- **Memory Pools**: Pre-allocated object pools for orders

### CPU Optimizations

- **CPU Affinity**: Pin threads to specific cores
- **NUMA Awareness**: Allocate memory on same NUMA node
- **Prefetching**: Manual prefetch instructions for order book traversal

### Compiler Optimizations

- **PGO**: Profile-guided optimization for hot paths
- **LTO**: Link-time optimization for cross-module inlining
- **march=native**: CPU-specific instructions (AVX2, etc.)

## Optional Advanced Features

### DPDK (Data Plane Development Kit)

- **Why**: Kernel bypass for ultra-low latency
- **When to Use**: If targeting < 1μs latency
- **Trade-off**: Requires dedicated CPU cores

### RDMA (Remote Direct Memory Access)

- **Why**: Zero-copy networking, CPU bypass
- **When to Use**: For inter-server communication
- **Trade-off**: Requires special hardware

### FPGA Integration

- **Why**: Hardware acceleration for critical paths
- **When to Use**: For production systems needing < 100ns
- **Trade-off**: Complex development, expensive hardware
