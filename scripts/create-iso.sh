#!/bin/bash
# Foxiwium OS - Create Bootable ISO with GRUB (BIOS)
# Uses grub-mkrescue to create a bootable ISO image

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
ISO_IMG="$BUILD_DIR/foxiwium.iso"

echo "[iso] Building bootable ISO..."

mkdir -p "$BUILD_DIR"

# Prepare ISO directory structure (fresh staging dir; avoids stale/root-owned leftovers)
ISO_DIR="$(mktemp -d "$BUILD_DIR/isodir.XXXXXX")"
trap 'rm -rf "$ISO_DIR"' EXIT
mkdir -p "$ISO_DIR/boot/grub"

# Copy kernel
cp "$BUILD_DIR/foxiwium.bin" "$ISO_DIR/boot/foxiwium.bin"

# Copy initramfs
if [ -f "$BUILD_DIR/initramfs.img" ]; then
    cp "$BUILD_DIR/initramfs.img" "$ISO_DIR/boot/initramfs.img"
fi

# Copy GRUB config
cp "$PROJECT_DIR/grub/grub.cfg" "$ISO_DIR/boot/grub/grub.cfg"

# Build ISO with grub-mkrescue
grub-mkrescue \
    -o "$ISO_IMG" \
    -R \
    "$ISO_DIR"

echo "[iso] Created: $ISO_IMG ($(du -h "$ISO_IMG" | cut -f1))"
echo "[iso] To run: qemu-system-x86_64 -cdrom $ISO_IMG -m 512M"
