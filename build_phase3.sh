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
SOURCE="test/test_stat_arb_system.cpp"
OUTPUT="test_phase3"

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

$COMPILER $CXXFLAGS $INCLUDES -o $OUTPUT $SOURCE

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Compilation successful!${NC}"
    echo ""
    
    # Check if user wants to run immediately
    if [ "$1" == "run" ]; then
        echo "=========================================="
        echo "Running Phase 3 Test Program"
        echo "=========================================="
        echo ""
        ./$OUTPUT
        EXIT_CODE=$?
        
        if [ $EXIT_CODE -eq 0 ]; then
            echo ""
            echo -e "${GREEN}=========================================="
            echo "Test completed successfully!"
            echo "==========================================${NC}"
        else
            echo ""
            echo -e "${RED}=========================================="
            echo "Test failed with exit code: $EXIT_CODE"
            echo "==========================================${NC}"
        fi
    else
        echo "Build complete. To run the test program:"
        echo "  ./$OUTPUT"
        echo ""
        echo "Or rebuild and run with:"
        echo "  ./build_phase3.sh run"
    fi
else
    echo -e "${RED}✗ Compilation failed${NC}"
    echo ""
    echo "Common issues:"
    echo "  1. Missing header files - ensure all Phase 1-3 headers are in place"
    echo "  2. Compiler version - ensure C++17 support"
    echo "  3. Missing dependencies - check all includes are available"
    echo ""
    echo "For detailed error output, run:"
    echo "  $COMPILER $CXXFLAGS $INCLUDES -o $OUTPUT $SOURCE 2>&1 | less"
    exit 1
fi


