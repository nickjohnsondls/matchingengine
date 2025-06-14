#!/bin/bash

# Simple build script for core data structures

set -e  # Exit on error

echo "=== Building Core Data Structures ==="

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring..."
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
echo "Building..."
make -j$(nproc)

# Run tests
echo "Running tests..."
./bin/test_order

echo "=== Build complete ===" 