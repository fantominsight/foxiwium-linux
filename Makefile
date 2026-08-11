# Foxiwium OS - Makefile
# Build system for x86_64 kernel with cross-compiler

# Cross-compiler (fall back to host tools if the x86_64-elf toolchain is missing)
CROSS   ?= /home/licheng/os/toolchain/sysroot/bin/x86_64-elf
ifeq ($(shell command -v $(CROSS)-g++ 2>/dev/null),)
CROSS   :=
endif
CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
AS      := $(CROSS)as
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
NM      := $(CROSS)nm

# Flags
CFLAGS  := -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
           -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 \
           -fno-pic -fno-pie -no-pie \
           -Wall -Wextra -Werror -g -O2 \
           -MMD -MP \
           -I kernel
CXXFLAGS := $(CFLAGS) -fno-use-cxa-atexit -fno-threadsafe-statics -fno-exceptions -fno-rtti

# Directories
SRCDIR   := kernel
BUILDDIR := build
SCRIPTS  := scripts
LINKER   := $(SRCDIR)/linker.ld
LDFLAGS  := -T $(LINKER) -nostdlib -z max-page-size=0x1000 -z noexecstack

# Sources (exclude userspace flat binaries)
ASM_SOURCES := $(filter-out $(SRCDIR)/userspace/%, $(shell find $(SRCDIR) -name '*.asm' | sort))
CXX_SOURCES := $(shell find $(SRCDIR) -name '*.cpp' | sort)
C_SOURCES   := $(shell find $(SRCDIR) -name '*.c' | sort)

# Objects
ASM_OBJECTS := $(patsubst $(SRCDIR)/%.asm,$(BUILDDIR)/%.o,$(ASM_SOURCES))
CXX_OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(CXX_SOURCES))
C_OBJECTS   := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SOURCES))

OBJECTS := $(ASM_OBJECTS) $(CXX_OBJECTS) $(C_OBJECTS)

# Output
KERNEL   := $(BUILDDIR)/foxiwium.bin
SHELL_BIN := $(BUILDDIR)/shell.bin
ISO      := $(BUILDDIR)/foxiwium.iso

# QEMU
QEMU     := qemu-system-x86_64
QEMUFLAGS := -m 512M -display sdl -accel kvm -nic user,model=rtl8139

.PHONY: all clean kernel iso disk initramfs shell run run-nographic run-disk debug

all: iso
	@echo ""
	@echo "==========================================================="
	@echo "[done] SINGLE BOOTABLE IMAGE FOR VM: $(ISO)"
	@echo "      Contains GRUB + kernel + initramfs."
	@echo "      Run:  $(QEMU) -cdrom $(ISO) -m 512M"
	@echo "==========================================================="

# ---- Kernel ----
kernel: $(KERNEL)
	@echo "[kernel] Built: $(KERNEL)"

$(KERNEL): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[kernel] Linked: $@ ($(shell stat -c%s $@ 2>/dev/null || echo '?') bytes)"

$(BUILDDIR)/%.o: $(SRCDIR)/%.asm
	@mkdir -p $(dir $@)
	nasm -f elf64 -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(OBJECTS:.o=.d)

# ---- Shell (userspace: пользовательский процесс Xandr) ----
# Бинарь собирается в репозитории Xandr и встраивается как "shell".
XANDR_DIR ?= /home/licheng/LC/foxiwium-linux/Xandr
XANDR_BIN := $(XANDR_DIR)/build/x18.bin

shell: $(SHELL_BIN)
	@echo "[shell] Built: $(SHELL_BIN)"

$(XANDR_BIN):
	$(MAKE) -C $(XANDR_DIR) os

$(SHELL_BIN): $(XANDR_BIN)
	@mkdir -p $(dir $@)
	cp $(XANDR_BIN) $@
	xxd -i $@ | sed 's/build_shell_bin/shell_binary/g; s/build_shell_bin_len/shell_binary_len/g' > kernel/userspace/shell_bin.h
	@echo "[shell] Embedded: $$(wc -c < $@) bytes"

# ---- Initramfs ----
initramfs:
	@$(SCRIPTS)/build-initramfs.sh

# ---- ISO ----
iso: kernel shell initramfs
	@$(SCRIPTS)/create-iso.sh

# ---- Disk Image ----
disk: kernel initramfs
	@$(SCRIPTS)/create-disk.sh

# ---- QEMU ----
run: iso
	$(QEMU) -cdrom $(ISO) $(QEMUFLAGS)

run-nographic: iso
	$(QEMU) -cdrom $(ISO) $(QEMUFLAGS) -nographic -serial stdio

run-disk: disk
	$(QEMU) -drive file=$(BUILDDIR)/foxiwium.img,format=raw $(QEMUFLAGS)

# ---- Debug ----
debug: iso
	$(QEMU) -cdrom $(ISO) $(QEMUFLAGS) -S -s &
	$(CROSS)-gdb -ex "target remote :1234" $(KERNEL)

# ---- Clean ----
clean:
	rm -rf $(BUILDDIR)

# ---- Info ----
info:
	@echo "=== Foxiwium OS Build System ==="
	@echo "Cross-compiler: $(CC)"
	@echo "Sources:  ASM=$(words $(ASM_SOURCES))  CXX=$(words $(CXX_SOURCES))  C=$(words $(C_SOURCES))"
	@echo "Objects:  $(words $(OBJECTS))"
	@echo "Output:   $(KERNEL)"
	@echo "ISO:      $(ISO)"
