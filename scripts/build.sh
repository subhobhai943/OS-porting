#!/bin/bash
# AAAos Build Script
# Builds the entire operating system

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "================================"
echo "AAAos Build Script"
echo "================================"
echo ""

# Check for required tools
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "Error: $1 is required but not installed."
        return 1
    fi
    echo "Found: $1"
}

echo "Checking build tools..."
check_tool nasm
check_tool make

# Check for cross-compiler (preferred) or native gcc
if command -v x86_64-elf-gcc &> /dev/null; then
    echo "Found: x86_64-elf-gcc (cross-compiler)"
    export CROSS_PREFIX="x86_64-elf-"
elif command -v gcc &> /dev/null; then
    echo "Found: gcc (native, will use with freestanding flags)"
    export CROSS_PREFIX=""
else
    echo "Error: No suitable C compiler found."
    exit 1
fi

echo ""
echo "Building AAAos..."
cd "$PROJECT_DIR"
make clean
make all

echo ""
echo "================================"
echo "Build complete!"
echo "================================"
echo ""
echo "Output files:"
echo "  - build/aaaos.img (bootable disk image)"
echo ""
echo "To run in QEMU:"
echo "  make run"
echo "  or"
echo "  ./scripts/run-qemu.sh"
