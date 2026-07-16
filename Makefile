TOOLCHAIN_VERSION := 1
QEMU ?= $(if $(wildcard $(HOME)/qemu-bins/bin/qemu-system-x86_64),$(HOME)/qemu-bins/bin/qemu-system-x86_64,qemu-system-x86_64)
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
PYTHON ?= python3
BUILD_DIR := cmake-build-debug
KERNEL_DIR := kernel
POLICY_SOURCE := policy/base.policy
POLICY_INC := $(BUILD_DIR)/base_policy.inc
KERNEL_ELF := $(BUILD_DIR)/kernel-payload.elf
KERNEL_BIN := $(BUILD_DIR)/kernel-payload.bin
LOADER_OBJ := $(BUILD_DIR)/multiboot_loader.o
PAYLOAD_OBJ := $(BUILD_DIR)/kernel_payload.o
BOOT_ELF := $(BUILD_DIR)/glassos.elf
CFLAGS := -m64 -ffreestanding -O2 -Wall -Wextra -Werror -std=c11 -nostdlib -mno-red-zone -mno-sse -mno-sse2 -fno-exceptions -fomit-frame-pointer -fno-pie -fno-stack-protector
LDFLAGS := -T linker.ld -nostdlib
C_SOURCES := $(wildcard $(KERNEL_DIR)/*.c)
C_OBJECTS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS := $(BUILD_DIR)/kernel_entry.o $(BUILD_DIR)/isr.o
.PHONY: all clean run run-headless test-boot test-truth test-config test-user test-policy-deny test-fault verify-layout toolchain
all: $(BOOT_ELF) verify-layout
$(BUILD_DIR):
	@mkdir -p $@
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -I$(BUILD_DIR) -c $< -o $@
$(BUILD_DIR)/policy.o: $(POLICY_INC)
$(POLICY_INC): $(POLICY_SOURCE) tests/embed_policy.py | $(BUILD_DIR)
	$(PYTHON) tests/embed_policy.py $(POLICY_SOURCE) $@
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.asm | $(BUILD_DIR)
	nasm -f elf64 $< -o $@
$(KERNEL_ELF): $(ASM_OBJECTS) $(C_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) $(ASM_OBJECTS) $(C_OBJECTS) -o $@
$(KERNEL_BIN): $(KERNEL_ELF)
	x86_64-elf-objcopy -O binary $< $@
$(PAYLOAD_OBJ): $(KERNEL_BIN)
	x86_64-elf-objcopy -I binary -O elf32-i386 -B i386 --rename-section .data=.kernel,alloc,load,readonly,data,contents --redefine-sym _binary_cmake_build_debug_kernel_payload_bin_start=kernel_payload $< $@
$(LOADER_OBJ): boot/multiboot_loader.asm | $(BUILD_DIR)
	nasm -f elf32 $< -o $@
$(BOOT_ELF): $(LOADER_OBJ) $(PAYLOAD_OBJ) boot/linker32.ld
	x86_64-elf-ld -m elf_i386 -T boot/linker32.ld $(LOADER_OBJ) $(PAYLOAD_OBJ) -o $@
verify-layout: $(KERNEL_ELF)
	@$(PYTHON) tests/verify_elf.py $(KERNEL_ELF)
run: all
	$(QEMU) -kernel $(BOOT_ELF) -serial file:$(BUILD_DIR)/serial.log -monitor none -no-reboot -no-shutdown
run-headless: all
	$(QEMU) -kernel $(BOOT_ELF) -display none -serial stdio -monitor none -no-reboot -no-shutdown
test-boot: all
	QEMU="$(QEMU)" $(PYTHON) tests/boot_test.py $(BOOT_ELF)
test-truth: all
	QEMU="$(QEMU)" $(PYTHON) tests/truth_transaction_test.py $(BOOT_ELF)
test-config: all
	QEMU="$(QEMU)" $(PYTHON) tests/config_transaction_test.py $(BOOT_ELF)
test-user: all
	QEMU="$(QEMU)" $(PYTHON) tests/user_mode_test.py $(BOOT_ELF)
test-policy-deny: clean
	$(MAKE) EXTRA_CFLAGS=-DGLASS_TEST_POLICY_DENY all
	QEMU="$(QEMU)" $(PYTHON) tests/policy_deny_test.py $(BOOT_ELF)
test-fault: clean
	$(MAKE) EXTRA_CFLAGS=-DGLASS_TEST_FAULT_INJECTION test-boot
toolchain:
	@echo "GlassOS toolchain workflow v$(TOOLCHAIN_VERSION): x86_64-elf GCC/binutils + NASM + QEMU x86_64"
clean:
	rm -rf $(BUILD_DIR) glass.img
