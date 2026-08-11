#!/bin/bash
# Foxiwium OS - Create Bootable Disk Image
# Partition layout:
#   1: /boot     - ext4     (512MB)
#   2: swap      - linux-swap (256MB)
#   3: /         - btrfs    (rest)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
DISK_IMG="$BUILD_DIR/foxiwium.img"
DISK_SIZE_MB=2048

echo "[disk] Creating Foxiwium disk image (${DISK_SIZE_MB}MB)..."

mkdir -p "$BUILD_DIR"

# Create raw disk image
qemu-img create -f raw "$DISK_IMG" "${DISK_SIZE_MB}M"

# Partition with parted (MBR for BIOS + GRUB)
# For pure UEFI we'd use GPT, but BIOS+multiboot2 uses MBR
parted -s "$DISK_IMG" mklabel msdos
parted -s "$DISK_IMG" mkpart primary ext4   1MiB 513MiB    # /boot
parted -s "$DISK_IMG" mkpart primary linux-swap 513MiB 769MiB  # swap
parted -s "$DISK_IMG" mkpart primary btrfs  769MiB 100%   # /

echo "[disk] Partitions created. Installing GRUB (BIOS)..."

# Setup loop device
LOOPDEV=$(sudo losetup -fP --show "$DISK_IMG")

# Format partitions
sudo mkfs.ext4 -F -L foxiwium-boot "${LOOPDEV}p1"
sudo mkswap -L foxiwium-swap "${LOOPDEV}p2"
sudo mkfs.btrfs -f -L foxiwium-root "${LOOPDEV}p3"

# Mount
MNT=$(mktemp -d)
sudo mkdir -p "$MNT/boot"
sudo mkdir -p "$MNT/root"

sudo mount "${LOOPDEV}p1" "$MNT/boot"
sudo mount "${LOOPDEV}p3" "$MNT/root"

# Create root filesystem structure
sudo mkdir -p "$MNT/root"/{bin,sbin,etc,dev,proc,sys,tmp,usr/{bin,lib,share}}
sudo mkdir -p "$MNT/root/boot/grub"

# Install kernel
sudo cp "$BUILD_DIR/foxiwium.bin" "$MNT/boot/foxiwium.bin"

# Install initramfs
if [ -f "$BUILD_DIR/initramfs.img" ]; then
    sudo cp "$BUILD_DIR/initramfs.img" "$MNT/boot/initramfs.img"
fi

# Install GRUB config
sudo cp "$PROJECT_DIR/grub/grub.cfg" "$MNT/boot/grub/grub.cfg"

# Install GRUB bootloader
sudo grub-install --target=i386-pc \
    --boot-directory="$MNT/boot" \
    --no-floppy \
    "$LOOPDEV"

echo "[disk] Disk image ready: $DISK_IMG"
echo "[disk] To run: qemu-system-x86_64 -drive file=$DISK_IMG,format=raw -m 512M"

# Cleanup
sudo umount "$MNT/boot" "$MNT/root"
sudo losetup -d "$LOOPDEV"
rm -rf "$MNT"
