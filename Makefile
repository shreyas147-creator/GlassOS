QEMU ?= $(if $(wildcard $(HOME)/qemu-bins/bin/qemu-system-x86_64),$(HOME)/qemu-bins/bin/qemu-system-x86_64,qemu-system-x86_64)
CC      = x86_64-elf-gcc
LD      = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy

BUILD_DIR  = cmake-build-debug
BOOT_DIR   = boot
KERNEL_DIR = kernel

BOOT_BIN   = $(BUILD_DIR)/boot.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
IMAGE      = glass.img
STAGE2_SIZE = 24576
KERNEL_SECTORS = 128
KERNEL_MAX_SIZE = 65536

CFLAGS  = -m64 -ffreestanding -O2 -Wall -Wextra -std=c11 -nostdlib -mno-red-zone -mno-sse -mno-sse2 -fno-exceptions -fomit-frame-pointer
LDFLAGS = -T linker.ld -nostdlib

C_SOURCES = $(wildcard $(KERNEL_DIR)/*.c)
C_OBJECTS = $(patsubst $(KERNEL_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(BUILD_DIR)/kernel_entry.o $(BUILD_DIR)/isr.o

.PHONY: all clean run setup

all: setup $(IMAGE)

setup:
	@mkdir -p $(BUILD_DIR)

$(BOOT_BIN): $(BOOT_DIR)/boot.asm
	nasm -f bin $< -o $@

$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm
	nasm -f bin $< -o $@

$(KERNEL_ELF): $(ASM_OBJECTS) $(C_OBJECTS)
	$(LD) $(LDFLAGS) $(ASM_OBJECTS) $(C_OBJECTS) -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.asm
	nasm -f elf64 $< -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	rm -f $(IMAGE)
	@test $$(wc -c < $(STAGE2_BIN)) -le $(STAGE2_SIZE) || { echo "stage2.bin is larger than $(STAGE2_SIZE) bytes; update boot/boot.asm sector layout"; exit 1; }
	@test $$(wc -c < $(KERNEL_BIN)) -le $(KERNEL_MAX_SIZE) || { echo "kernel.bin is larger than $(KERNEL_MAX_SIZE) bytes; update boot/boot.asm KERNEL_SECTORS"; exit 1; }
	# Stage 1 loads exactly 48 sectors for stage 2, then 128 sectors for the kernel.
	$(OBJCOPY) --gap-fill=0x00 --pad-to=$(STAGE2_SIZE) -I binary -O binary $(STAGE2_BIN) $(BUILD_DIR)/stage2_padded.bin
	cat $(BOOT_BIN) $(BUILD_DIR)/stage2_padded.bin $(KERNEL_BIN) > $(IMAGE)
	truncate -s 1440k $(IMAGE)

clean:
	rm -rf $(BUILD_DIR) $(IMAGE)

run: all
	$(QEMU) -drive format=raw,file=$(IMAGE)
