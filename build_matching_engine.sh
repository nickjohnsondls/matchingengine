#!/bin/bash

# Build script for matching engine

set -e  # Exit on error

echo "Building matching engine..."

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build core library
echo "Building core library..."
make micromatch_core -j$(nproc)

# Build tests
echo "Building tests..."
make test_orderbook test_matching_engine -j$(nproc)

# Run tests
echo "Running orderbook tests..."
./test_orderbook

echo "Running matching engine tests..."
./test_matching_engine

echo "Build complete!" 