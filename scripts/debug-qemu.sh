#!/bin/bash
# AAAos QEMU Debug Script
# Runs the OS in QEMU with GDB server enabled

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE="$PROJECT_DIR/build/aaaos.img"
KERNEL="$PROJECT_DIR/build/kernel.bin"

# Check if image exists
if [ ! -f "$IMAGE" ]; then
    echo "Error: Disk image not found at $IMAGE"
    echo "Please build the project first: make all"
    exit 1
fi

# Check for QEMU
if ! command -v qemu-system-x86_64 &> /dev/null; then
    echo "Error: qemu-system-x86_64 is not installed."
    exit 1
fi

echo "Starting AAAos in QEMU debug mode..."
echo ""
echo "QEMU is waiting for GDB connection on localhost:1234"
echo "In another terminal, run:"
echo ""
echo "  gdb -ex 'target remote localhost:1234' $KERNEL"
echo ""
echo "Useful GDB commands:"
echo "  c          - Continue execution"
echo "  b *0x100000 - Set breakpoint at kernel entry"
echo "  si         - Single step instruction"
echo "  info reg   - Show registers"
echo "  x/10i \$rip - Disassemble at current instruction"
echo ""
echo "Press Ctrl+C to stop QEMU"
echo ""

# Run QEMU with:
# - GDB server on port 1234
# - Paused at startup (-S)
qemu-system-x86_64 \
    -drive format=raw,file="$IMAGE" \
    -serial stdio \
    -m 256M \
    -no-reboot \
    -no-shutdown \
    -s \
    -S \
    "$@"
