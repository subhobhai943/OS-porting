# AAAos Main Makefile
# Cross-compilation for x86_64

# Toolchain (use cross-compiler if available, fall back to native)
CROSS_PREFIX ?= x86_64-elf-
CC := $(CROSS_PREFIX)gcc
AS := nasm
LD := $(CROSS_PREFIX)ld
OBJCOPY := $(CROSS_PREFIX)objcopy

# Check if cross-compiler exists, fall back to native gcc with appropriate flags
CROSS_CHECK := $(shell which $(CC) 2>/dev/null)
ifeq ($(CROSS_CHECK),)
    CC := gcc
    LD := ld
    OBJCOPY := objcopy
endif

# Directories
BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
BOOT_DIR := boot
KERNEL_DIR := kernel

# Output files
BOOTLOADER := $(BUILD_DIR)/boot.bin
KERNEL := $(BUILD_DIR)/kernel.bin
OS_IMAGE := $(BUILD_DIR)/aaaos.img
ISO_IMAGE := $(BUILD_DIR)/aaaos.iso

# Compiler flags
CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
          -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel \
          -Wall -Wextra -Werror -std=gnu11 -O2 -g \
          -I$(KERNEL_DIR)/include -I$(KERNEL_DIR)/arch/x86_64/include

LDFLAGS := -nostdlib -z max-page-size=0x1000

ASFLAGS := -f elf64

# Bootloader assembly flags (16-bit and 32-bit modes)
BOOT_ASFLAGS := -f bin

# Source files
BOOT_STAGE1_SRC := $(BOOT_DIR)/bios/stage1.asm
BOOT_STAGE2_SRC := $(BOOT_DIR)/bios/stage2.asm

KERNEL_ASM_SRCS := $(shell find $(KERNEL_DIR) -name '*.asm' 2>/dev/null)
KERNEL_C_SRCS := $(shell find $(KERNEL_DIR) -name '*.c' 2>/dev/null)

KERNEL_ASM_OBJS := $(patsubst %.asm,$(BUILD_DIR)/%.o,$(KERNEL_ASM_SRCS))
KERNEL_C_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_OBJS := $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS)

# Default target
.PHONY: all
all: $(OS_IMAGE)

# Create build directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(BOOT_DIR)/bios:
	mkdir -p $(BUILD_DIR)/$(BOOT_DIR)/bios

$(BUILD_DIR)/$(KERNEL_DIR):
	mkdir -p $(BUILD_DIR)/$(KERNEL_DIR)

# Bootloader Stage 1 (MBR - 512 bytes)
$(BUILD_DIR)/stage1.bin: $(BOOT_STAGE1_SRC) | $(BUILD_DIR)/$(BOOT_DIR)/bios
	$(AS) $(BOOT_ASFLAGS) $< -o $@

# Bootloader Stage 2
$(BUILD_DIR)/stage2.bin: $(BOOT_STAGE2_SRC) | $(BUILD_DIR)/$(BOOT_DIR)/bios
	$(AS) $(BOOT_ASFLAGS) $< -o $@

# Kernel assembly files
$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Kernel C files
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link kernel
$(KERNEL): $(KERNEL_OBJS) $(KERNEL_DIR)/linker.ld
	$(LD) $(LDFLAGS) -T $(KERNEL_DIR)/linker.ld -o $@ $(KERNEL_OBJS)

# Create OS disk image
$(OS_IMAGE): $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin $(KERNEL)
	@echo "Creating disk image..."
	# Create 64MB disk image
	dd if=/dev/zero of=$@ bs=1M count=64 2>/dev/null
	# Write Stage 1 to MBR
	dd if=$(BUILD_DIR)/stage1.bin of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	# Write Stage 2 starting at sector 2
	dd if=$(BUILD_DIR)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	# Write kernel starting at sector 10
	dd if=$(KERNEL) of=$@ bs=512 seek=9 conv=notrunc 2>/dev/null
	@echo "Disk image created: $@"

# Create bootable ISO
.PHONY: iso
iso: $(OS_IMAGE)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.bin
	echo 'menuentry "AAAos" { multiboot /boot/kernel.bin }' > $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR) 2>/dev/null || echo "grub-mkrescue not available"

# Run in QEMU
.PHONY: run
run: $(OS_IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE) -serial stdio -m 256M

# Run with KVM acceleration (Linux only)
.PHONY: run-kvm
run-kvm: $(OS_IMAGE)
	qemu-system-x86_64 -enable-kvm -drive format=raw,file=$(OS_IMAGE) -serial stdio -m 256M

# Debug with GDB
.PHONY: debug
debug: $(OS_IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE) -serial stdio -m 256M -s -S &
	@echo "QEMU started. Connect with: gdb -ex 'target remote localhost:1234'"

# Debug with curses display (for headless)
.PHONY: debug-curses
debug-curses: $(OS_IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE) -serial stdio -m 256M -display curses -s -S

# Clean build files
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# Module-specific builds
.PHONY: kernel
kernel: $(KERNEL)

.PHONY: bootloader
bootloader: $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin

# Tests
.PHONY: test
test:
	@echo "Running all tests..."
	$(MAKE) -C tests all

.PHONY: test-mm
test-mm:
	@echo "Memory manager unit tests are not wired in tests/Makefile yet"

.PHONY: test-fs
test-fs:
	@echo "Filesystem unit tests are not wired in tests/Makefile yet"

# Experimental ARMv7 (32-bit) kernel port build
ARMV7_PREFIX ?= arm-none-eabi-
ARMV7_CC := $(ARMV7_PREFIX)gcc
ARMV7_LD := $(ARMV7_PREFIX)ld
ARMV7_AS := $(ARMV7_PREFIX)as

ARMV7_CFLAGS := -ffreestanding -fno-stack-protector -nostdlib \
	-marm -march=armv7-a -mabi=aapcs -Wall -Wextra -Werror -O2 \
	-I$(KERNEL_DIR)/include -I$(KERNEL_DIR)/arch/armv7/include

ARMV7_BUILD_DIR := $(BUILD_DIR)/armv7
ARMV7_OBJS := $(ARMV7_BUILD_DIR)/kernel/arch/armv7/boot.o \
	$(ARMV7_BUILD_DIR)/kernel/main_armv7.o
ARMV7_KERNEL := $(ARMV7_BUILD_DIR)/kernel-armv7.elf

.PHONY: kernel-armv7
kernel-armv7:
	@mkdir -p $(ARMV7_BUILD_DIR)/kernel/arch/armv7
	@if ! command -v $(ARMV7_CC) >/dev/null 2>&1; then \
		echo "ARMv7 toolchain not found: $(ARMV7_CC)"; \
		echo "Install $(ARMV7_PREFIX)gcc/binutils and re-run 'make kernel-armv7'"; \
		exit 1; \
	fi
	$(ARMV7_AS) kernel/arch/armv7/boot.S -o $(ARMV7_BUILD_DIR)/kernel/arch/armv7/boot.o
	$(ARMV7_CC) $(ARMV7_CFLAGS) -c kernel/main_armv7.c -o $(ARMV7_BUILD_DIR)/kernel/main_armv7.o
	$(ARMV7_LD) -nostdlib -T kernel/linker_armv7.ld -o $(ARMV7_KERNEL) $(ARMV7_OBJS)
	@echo "ARMv7 kernel built: $(ARMV7_KERNEL)"

# Help
.PHONY: help
help:
	@echo "AAAos Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build everything (default)"
	@echo "  run          - Run in QEMU"
	@echo "  run-kvm      - Run in QEMU with KVM"
	@echo "  debug        - Run with GDB server"
	@echo "  iso          - Create bootable ISO"
	@echo "  clean        - Remove build files"
	@echo "  kernel       - Build kernel only"
	@echo "  bootloader   - Build bootloader only"
	@echo "  test         - Run all tests"
	@echo "  help         - Show this help"
