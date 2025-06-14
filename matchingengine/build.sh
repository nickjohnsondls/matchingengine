#!/bin/bash

# Build script for MicroMatch matching engine

set -e  # Exit on error

echo "=== Building MicroMatch Matching Engine ==="

# Create build directory
mkdir -p build
cd build

# Install dependencies with Conan
echo "Installing dependencies with Conan..."
conan install .. --build=missing -s compiler.libcxx=libstdc++11

# Configure with CMake
echo "Configuring with CMake..."
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# Build
echo "Building..."
ninja -j$(nproc)

# Run tests
echo "Running tests..."
ctest --output-on-failure

echo "=== Build complete ==="
echo "Binary location: build/bin/matching_engine"
echo "Run with: ./bin/matching_engine --help" 