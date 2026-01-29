#!/bin/bash
# AAAos QEMU Launch Script
# Runs the OS in QEMU emulator

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE="$PROJECT_DIR/build/aaaos.img"

# Check if image exists
if [ ! -f "$IMAGE" ]; then
    echo "Error: Disk image not found at $IMAGE"
    echo "Please build the project first: make all"
    exit 1
fi

# Check for QEMU
if ! command -v qemu-system-x86_64 &> /dev/null; then
    echo "Error: qemu-system-x86_64 is not installed."
    echo "Please install QEMU:"
    echo "  Ubuntu/Debian: sudo apt install qemu-system-x86"
    echo "  Fedora: sudo dnf install qemu-system-x86"
    echo "  macOS: brew install qemu"
    echo "  Windows: Download from https://www.qemu.org/"
    exit 1
fi

echo "Starting AAAos in QEMU..."
echo "Press Ctrl+A then X to exit QEMU"
echo ""

# Run QEMU with:
# - 256MB RAM
# - Serial output to stdio
# - Boot from disk image
qemu-system-x86_64 \
    -drive format=raw,file="$IMAGE" \
    -serial stdio \
    -m 256M \
    -no-reboot \
    -no-shutdown \
    "$@"
