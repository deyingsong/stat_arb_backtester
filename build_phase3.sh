#!/bin/bash
# build_phase3.sh
# Build and run script for Phase 3: Statistical Arbitrage Features

echo "=========================================="
echo "Phase 3: Statistical Arbitrage Build Script"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check for required directories
echo "Checking directory structure..."
if [ ! -d "include" ]; then
    echo -e "${YELLOW}Creating include directory...${NC}"
    mkdir -p include
fi

if [ ! -d "include/strategies" ]; then
    echo -e "${YELLOW}Creating include/strategies directory...${NC}"
    mkdir -p include/strategies
fi

if [ ! -d "include/execution" ]; then
    echo -e "${YELLOW}Creating include/execution directory...${NC}"
    mkdir -p include/execution
fi

if [ ! -d "data" ]; then
    echo -e "${YELLOW}Creating data directory...${NC}"
    mkdir -p data
fi

if [ ! -d "test" ]; then
    echo -e "${YELLOW}Creating test directory...${NC}"
    mkdir -p test
fi

echo -e "${GREEN}✓ Directory structure verified${NC}"
echo ""

# Check for compiler
echo "Checking for C++ compiler..."
if command -v g++ &> /dev/null; then
    COMPILER="g++"
    echo -e "${GREEN}✓ Found g++${NC}"
elif command -v clang++ &> /dev/null; then
    COMPILER="clang++"
    echo -e "${GREEN}✓ Found clang++${NC}"
else
    echo -e "${RED}✗ No C++ compiler found. Please install g++ or clang++${NC}"
    exit 1
fi

# Check C++ version support
echo "Checking C++17 support..."
echo "#include <variant>" | $COMPILER -std=c++17 -x c++ -c - -o /dev/null 2>/dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ C++17 supported${NC}"
else
    echo -e "${RED}✗ C++17 not supported. Please upgrade your compiler.${NC}"
    exit 1
fi
echo ""

# Compilation flags
CXXFLAGS="-std=c++17 -O3 -Wall -Wextra -pthread"
INCLUDES="-I./include"
SOURCE_GLOB="test/*.cpp"
BIN_DIR="bin"

mkdir -p $BIN_DIR

# Clean previous build
if [ -f "$OUTPUT" ]; then
    echo "Cleaning previous build..."
    rm -f $OUTPUT
    echo -e "${GREEN}✓ Cleaned${NC}"
    echo ""
fi

# Compile
echo "Compiling Phase 3 test program..."
echo "Compiler: $COMPILER"
echo "Flags: $CXXFLAGS"
echo ""

FAILED=0
for src in $SOURCE_GLOB; do
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
done

if [ $FAILED -ne 0 ]; then
    echo -e "${RED}✗ One or more compilations failed${NC}"
    exit 1
else
    echo -e "${GREEN}✓ All tests compiled successfully${NC}"
fi

if [ "$1" == "run" ]; then
    echo "=========================================="
    echo "Running Test Binaries"
    echo "=========================================="
    echo ""
    overall_exit=0
    for exe in $BIN_DIR/*; do
        echo "Running $exe"
        "$exe"
        code=$?
        if [ $code -ne 0 ]; then
            echo -e "${RED}$exe failed (exit $code)${NC}"
            overall_exit=1
        else
            echo -e "${GREEN}$exe passed${NC}"
        fi
        echo ""
    done

    if [ $overall_exit -eq 0 ]; then
        echo -e "${GREEN}=========================================="
        echo "All tests passed!"
        echo "==========================================${NC}"
    else
        echo -e "${RED}=========================================="
        echo "One or more tests failed"
        echo "==========================================${NC}"
    fi
    exit $overall_exit
fi


