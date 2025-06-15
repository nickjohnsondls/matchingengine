#!/bin/bash

# Build script for lock-free queues

set -e  # Exit on error

echo "Building lock-free queues..."

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build tests
echo "Building queue tests..."
make test_queues -j$(nproc)

# Build benchmarks if available
if make help | grep -q benchmark_queues; then
    echo "Building benchmarks..."
    make benchmark_queues -j$(nproc)
fi

# Run tests
echo "Running queue tests..."
./test_queues

# Run benchmarks if built
if [ -f ./benchmark_queues ]; then
    echo "Running benchmarks..."
    echo "Note: This will take some time..."
    ./benchmark_queues --benchmark_min_time=1.0
fi

echo "Build complete!" 