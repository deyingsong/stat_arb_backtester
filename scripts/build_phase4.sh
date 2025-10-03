#!/bin/bash
# build_phase4.sh
# Build and run script for Phase 4: Performance Optimizations

echo "=========================================="
echo "Phase 4: Performance Optimization Build"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check for required directories
echo "Checking directory structure..."
for dir in "include/concurrent" "include/math" "include/core" "test" "bin"; do
    if [ ! -d "$dir" ]; then
        echo -e "${YELLOW}Creating $dir directory...${NC}"
        mkdir -p "$dir"
    fi
done

echo -e "${GREEN}✓ Directory structure verified${NC}"
echo ""

# Check for compiler
echo "Checking for C++ compiler..."
if command -v clang++ &> /dev/null; then
    COMPILER="clang++"
    echo -e "${GREEN}✓ Found clang++ (recommended for Apple Silicon)${NC}"
elif command -v g++ &> /dev/null; then
    COMPILER="g++"
    echo -e "${GREEN}✓ Found g++${NC}"
else
    echo -e "${RED}✗ No C++ compiler found. Please install clang++ or g++${NC}"
    exit 1
fi

# Detect CPU architecture
echo "Detecting CPU architecture..."
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]; then
    echo -e "${GREEN}✓ ARM64 detected (Apple Silicon)${NC}"
    ARCH_FLAGS="-march=armv8-a+simd"
    echo "  NEON SIMD support will be enabled"
elif [ "$ARCH" = "x86_64" ]; then
    echo -e "${GREEN}✓ x86-64 detected${NC}"
    ARCH_FLAGS="-march=native"
else
    echo -e "${YELLOW}⚠ Unknown architecture: $ARCH${NC}"
    ARCH_FLAGS=""
fi
echo ""

# Compilation flags for maximum performance
echo "Setting optimization flags..."
CXXFLAGS="-std=c++17 -O3 -Wall -Wextra -pthread $ARCH_FLAGS"
CXXFLAGS="$CXXFLAGS -ffast-math"  # Aggressive math optimizations
CXXFLAGS="$CXXFLAGS -funroll-loops"  # Loop unrolling
CXXFLAGS="$CXXFLAGS -finline-functions"  # Aggressive inlining

if [ "$COMPILER" = "clang++" ]; then
    CXXFLAGS="$CXXFLAGS -flto"  # Link-time optimization
fi

INCLUDES="-I./include"
BIN_DIR="bin"

echo "Compiler: $COMPILER"
echo "Flags: $CXXFLAGS"
echo ""

# Compile Phase 4 performance test
echo "Compiling Phase 4 Performance Test Suite..."
TEST_SOURCE="test/test_phase4_performance.cpp"
TEST_OUTPUT="$BIN_DIR/test_phase4_performance"

$COMPILER $CXXFLAGS $INCLUDES -o "$TEST_OUTPUT" "$TEST_SOURCE"

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ Failed to compile Phase 4 tests${NC}"
    exit 1
else
    echo -e "${GREEN}✓ Successfully compiled Phase 4 tests${NC}"
fi
echo ""

# Run benchmarks if requested
if [ "$1" == "run" ] || [ "$1" == "bench" ]; then
    echo "=========================================="
    echo "Running Performance Benchmarks"
    echo "=========================================="
    echo ""
    
    "$TEST_OUTPUT"
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}=========================================="
        echo "Benchmarks completed successfully!"
        echo "==========================================${NC}"
    else
        echo -e "${RED}=========================================="
        echo "Benchmark failed"
        echo "==========================================${NC}"
        exit 1
    fi
fi

# Also compile all other tests
if [ "$1" == "all" ]; then
    echo "=========================================="
    echo "Building All Tests"
    echo "=========================================="
    echo ""
    
    FAILED=0
    for src in test/test_*.cpp; do
        if [ -f "$src" ]; then
            exe_name=$(basename "$src" .cpp)
            out_path="$BIN_DIR/$exe_name"
            
            echo "Compiling $src -> $out_path"
            $COMPILER $CXXFLAGS $INCLUDES -o "$out_path" "$src"
            
            if [ $? -ne 0 ]; then
                echo -e "${RED}✗ Failed to compile $src${NC}"
                FAILED=1
            else
                echo -e "${GREEN}✓ Compiled $src${NC}"
            fi
        fi
    done
    
    if [ $FAILED -eq 0 ]; then
        echo -e "\n${GREEN}✓ All tests compiled successfully${NC}"
    else
        echo -e "\n${RED}✗ Some compilations failed${NC}"
        exit 1
    fi
fi

echo ""
echo "Build complete!"
echo ""
echo "Usage:"
echo "  ./build_phase4.sh          - Build Phase 4 tests only"
echo "  ./build_phase4.sh run      - Build and run performance benchmarks"
echo "  ./build_phase4.sh all      - Build all tests"
echo ""