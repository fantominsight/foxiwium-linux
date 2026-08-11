#!/bin/bash
# Foxiwium OS - Build Initramfs
# Creates a minimal initramfs image with busybox (or simple init)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
INITRAMFS_DIR="$PROJECT_DIR/initramfs"
INITRAMFS_IMG="$PROJECT_DIR/build/initramfs.img"

echo "[initramfs] Building initramfs..."

# Create build directory
mkdir -p "$PROJECT_DIR/build"

# Create minimal /init script
mkdir -p "$INITRAMFS_DIR"
cat > "$INITRAMFS_DIR/init" << 'EOF'
#!/bin/sh
# Foxiwium OS - Init Process (PID 1)

echo "=========================================="
echo "  Foxiwium OS - Initramfs Init"
echo "=========================================="

# Mount essential filesystems
mount -t proc     proc     /proc
mount -t sysfs    sysfs    /sys
mount -t devtmpfs devtmpfs /dev
mount -t tmpfs    tmpfs    /tmp

echo "[init] Filesystems mounted"

# Print boot info
cat /proc/version 2>/dev/null || echo "[init] No /proc/version"

# List devices
echo "[init] Available devices:"
ls /dev/ 2>/dev/null

echo ""
echo "[init] Initramfs phase complete."
echo "[init] Dropping to shell (busybox or manual)..."

# Try to exec into a shell
if [ -x /bin/sh ]; then
    exec /bin/sh
elif [ -x /bin/bash ]; then
    exec /bin/bash
else
    echo "[init] No shell found. Halting."
    while true; do
        hlt
    done
fi
EOF
chmod +x "$INITRAMFS_DIR/init"

# Pack initramfs
cd "$INITRAMFS_DIR"
find . -print0 | cpio --null -ov --format=newc 2>/dev/null > "$INITRAMFS_IMG"

echo "[initramfs] Created: $INITRAMFS_IMG ($(du -h "$INITRAMFS_IMG" | cut -f1))"
