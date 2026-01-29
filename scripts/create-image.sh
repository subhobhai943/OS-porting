#!/bin/bash
# AAAos Disk Image Creation Script
# Creates a bootable disk image from compiled binaries

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

STAGE1="$BUILD_DIR/stage1.bin"
STAGE2="$BUILD_DIR/stage2.bin"
KERNEL="$BUILD_DIR/kernel.bin"
OUTPUT="$BUILD_DIR/aaaos.img"

# Image size in MB
IMAGE_SIZE=64

echo "Creating AAAos disk image..."

# Check for required files
for file in "$STAGE1" "$STAGE2" "$KERNEL"; do
    if [ ! -f "$file" ]; then
        echo "Error: Required file not found: $file"
        exit 1
    fi
done

# Create empty disk image
echo "  Creating ${IMAGE_SIZE}MB disk image..."
dd if=/dev/zero of="$OUTPUT" bs=1M count=$IMAGE_SIZE 2>/dev/null

# Write Stage 1 bootloader (MBR, sector 0)
echo "  Writing Stage 1 bootloader..."
dd if="$STAGE1" of="$OUTPUT" bs=512 count=1 conv=notrunc 2>/dev/null

# Write Stage 2 bootloader (sectors 1-8)
echo "  Writing Stage 2 bootloader..."
dd if="$STAGE2" of="$OUTPUT" bs=512 seek=1 conv=notrunc 2>/dev/null

# Write kernel (starting at sector 9)
echo "  Writing kernel..."
dd if="$KERNEL" of="$OUTPUT" bs=512 seek=9 conv=notrunc 2>/dev/null

# Print sizes
echo ""
echo "Image layout:"
echo "  Stage 1:  $(stat -f%z "$STAGE1" 2>/dev/null || stat -c%s "$STAGE1") bytes (sector 0)"
echo "  Stage 2:  $(stat -f%z "$STAGE2" 2>/dev/null || stat -c%s "$STAGE2") bytes (sectors 1-8)"
echo "  Kernel:   $(stat -f%z "$KERNEL" 2>/dev/null || stat -c%s "$KERNEL") bytes (sector 9+)"
echo ""
echo "Disk image created: $OUTPUT"
