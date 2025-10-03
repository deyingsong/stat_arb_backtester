#!/bin/bash
# build_phase5.sh
# Build and run script for Phase 5: Advanced Statistical Validation

echo "=========================================="
echo "Phase 5: Statistical Validation Build"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check for required directories
echo "Checking directory structure..."

REQUIRED_DIRS=(
    "include/validation"
    "test"
    "bin"
)

for dir in "${REQUIRED_DIRS[@]}"; do
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
    echo -e "${GREEN}✓ Found clang++ (recommended)${NC}"
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
elif [ "$ARCH" = "x86_64" ]; then
    echo -e "${GREEN}✓ x86-64 detected${NC}"
    ARCH_FLAGS="-march=native"
else
    echo -e "${YELLOW}⚠ Unknown architecture: $ARCH${NC}"
    ARCH_FLAGS=""
fi
echo ""

# Compilation flags
echo "Setting compilation flags..."
CXXFLAGS="-std=c++17 -O3 -Wall -Wextra -pthread $ARCH_FLAGS"
CXXFLAGS="$CXXFLAGS -ffast-math -funroll-loops -finline-functions"

INCLUDES="-I./include"
BIN_DIR="bin"

echo "Compiler: $COMPILER"
echo "Flags: $CXXFLAGS"
echo ""

# Create header files if they don't exist
echo "Checking for Phase 5 header files..."

HEADERS=(
    "include/validation/purged_cross_validation.hpp"
    "include/validation/deflated_sharpe_ratio.hpp"
)

MISSING_HEADERS=0
for header in "${HEADERS[@]}"; do
    if [ ! -f "$header" ]; then
        echo -e "${YELLOW}⚠ Missing: $header${NC}"
        MISSING_HEADERS=$((MISSING_HEADERS + 1))
    fi
done

if [ $MISSING_HEADERS -gt 0 ]; then
    echo -e "${YELLOW}Note: Please ensure all Phase 5 headers are in place${NC}"
    echo ""
fi

# Compile Phase 5 validation test
echo "Compiling Phase 5 Statistical Validation Suite..."
TEST_SOURCE="test/test_phase5_validation.cpp"
TEST_OUTPUT="$BIN_DIR/test_phase5_validation"

if [ ! -f "$TEST_SOURCE" ]; then
    echo -e "${YELLOW}Creating test source template...${NC}"
    echo "Test source needs to be created manually"
fi

$COMPILER $CXXFLAGS $INCLUDES -o "$TEST_OUTPUT" "$TEST_SOURCE" 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ Failed to compile Phase 5 validation suite${NC}"
    echo ""
    echo "Common issues:"
    echo "  1. Ensure all header files are in include/validation/"
    echo "  2. Check that test source exists in test/"
    echo "  3. Verify C++17 support"
    exit 1
else
    echo -e "${GREEN}✓ Successfully compiled Phase 5 validation suite${NC}"
fi
echo ""

# Run tests if requested
if [ "$1" == "run" ] || [ "$1" == "test" ]; then
    echo "=========================================="
    echo "Running Phase 5 Validation Tests"
    echo "=========================================="
    echo ""
    
    if [ -f "$TEST_OUTPUT" ]; then
        "$TEST_OUTPUT"
        
        if [ $? -eq 0 ]; then
            echo ""
            echo -e "${GREEN}=========================================="
            echo "Phase 5 tests completed successfully!"
            echo -e "==========================================${NC}"
            echo ""
            echo "Statistical validation tools are ready:"
            echo "  ✓ Purged K-Fold Cross-Validation"
            echo "  ✓ Combinatorial Purged CV (CPCV)"
            echo "  ✓ Deflated Sharpe Ratio (DSR)"
            echo "  ✓ Multiple Testing Adjustments"
            echo ""
        else
            echo ""
            echo -e "${RED}Phase 5 tests encountered errors${NC}"
            exit 1
        fi
    else
        echo -e "${RED}Test executable not found${NC}"
        exit 1
    fi
fi

# Display usage info
if [ "$1" != "run" ] && [ "$1" != "test" ]; then
    echo "Build completed successfully!"
    echo ""
    echo "To run the tests:"
    echo "  ./build_phase5.sh run"
    echo ""
    echo "Or execute directly:"
    echo "  ./$TEST_OUTPUT"
    echo ""
fi

echo "Phase 5 Implementation Summary:"
echo "================================"
echo ""
echo -e "${BLUE}Purged Cross-Validation:${NC}"
echo "  • Sequential time series splitting"
echo "  • Purging to prevent look-ahead bias"
echo "  • Embargo periods for serial correlation"
echo "  • Combinatorial exhaustive validation"
echo ""
echo -e "${BLUE}Deflated Sharpe Ratio:${NC}"
echo "  • Adjusts for multiple testing bias"
echo "  • Accounts for skewness and kurtosis"
echo "  • Probabilistic Sharpe Ratio (PSR)"
echo "  • Minimum track length calculation"
echo "  • Statistical significance testing"
echo ""
echo -e "${BLUE}Multiple Testing Corrections:${NC}"
echo "  • Bonferroni correction"
echo "  • Holm-Bonferroni sequential method"
echo "  • Benjamini-Hochberg FDR control"
echo ""
echo "These tools are critical for:"
echo "  • Preventing overfitting"
echo "  • Realistic strategy assessment"
echo "  • Research workflow discipline"
echo "  • Deployment decision-making"
echo ""