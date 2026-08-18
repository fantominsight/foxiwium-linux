#!/bin/bash
# Foxiwium Linux — build a bootable live ISO (GRUB BIOS + UEFI).
# ВЫПОЛНЯТЬ ТОЛЬКО И ТОЛЬКО В КОНТЕЙНЕР DOCKER, А ТО СНЕСЕШЬ МНЕ СИСТЕМУ!
# Layout produced on the ISO:
#   /boot/grub/grub.cfg
#   /live/vmlinuz
#   /live/initrd.img
#   /live/filesystem.squashfs     <- the whole rootfs, squashfs-compressed
#
# The kernel is booted with "boot=live": the initramfs (built by
# initramfs-tools + live-boot in build-rootfs.sh) finds the squashfs on the
# medium and pivot_root into it. Works in QEMU and on real hardware (BIOS/UEFI).

set -euo pipefail

# Headless builds: SUDOPASS enables sudo without a tty via an askpass helper.
if [[ -n "${SUDOPASS:-}" ]]; then
    ASKPASS="$(mktemp)"
    printf '#!/bin/sh\necho "%s"\n' "$SUDOPASS" > "$ASKPASS"
    chmod 700 "$ASKPASS"
    export SUDO_ASKPASS="$ASKPASS"
    sudo() { command sudo -A "$@"; }
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD="$PROJECT_DIR/build"
ROOTFS="$PROJECT_DIR/rootfs"
MARKER="$ROOTFS/.foxiwium-complete"
ISO="$BUILD/foxiwium.iso"
SQUASHFS_COMP="${SQUASHFS_COMP:-zstd}"

# Never hard-code /dev/null in redirects (see build-rootfs.sh): if /dev/null is
# broken, the redirect fails and the guarded command silently does not run.
NULL_TARGET=/dev/null
[[ -w "$NULL_TARGET" ]] || NULL_TARGET=/tmp/foxiwium-null

if [[ ! -f "$MARKER" ]]; then
    echo "[iso] rootfs not built yet — run 'make rootfs' first" >&2
    exit 1
fi

# List absolute targets of every mount at or under $1 ($1 need not be a
# mountpoint). NOTE: `findmnt -R "$ROOTFS"` silently prints NOTHING when the
# arg is a plain directory (it only works for real mountpoints), which is how
# the /dev-wipe guard used to fail — see build-rootfs.sh.
mounts_under() {
    local root="$1"
    findmnt -n -r -o TARGET | awk -v p="$root" '$0 == p || index($0, p "/") == 1'
}

# Safety: never compress $ROOTFS while host filesystems are bind-mounted inside
# it — mksquashfs would pull in the HOST's /dev, /sys and /proc.
if left="$(mounts_under "$ROOTFS" | head -n1)" && [[ -n "$left" ]]; then
    echo "[iso] FATAL: mount '$left' still present under $ROOTFS — aborting" >&2
    exit 1
fi

VMLINUZ="$(ls "$ROOTFS"/boot/vmlinuz-* 2>"$NULL_TARGET" | head -n1 || true)"
INITRD="$(ls "$ROOTFS"/boot/initrd.img-* 2>"$NULL_TARGET" | head -n1 || true)"
if [[ -z "$VMLINUZ" || -z "$INITRD" ]]; then
    echo "[iso] kernel or initrd not found in $ROOTFS/boot" >&2
    exit 1
fi

mkdir -p "$BUILD"
sudo -v

ISO_DIR="$(mktemp -d "$BUILD/isodir.XXXXXX")"
trap 'rm -rf "$ISO_DIR"; rm -f "${ASKPASS:-}"' EXIT
mkdir -p "$ISO_DIR/live" "$ISO_DIR/boot/grub"

echo "[iso] kernel:  $VMLINUZ"
echo "[iso] initrd:  $INITRD"
cp "$VMLINUZ" "$ISO_DIR/live/vmlinuz"
cp "$INITRD"  "$ISO_DIR/live/initrd.img"

echo "[iso] squashfs rootfs ($SQUASHFS_COMP) ..."
sudo mksquashfs "$ROOTFS" "$ISO_DIR/live/filesystem.squashfs" \
    -noappend -comp "$SQUASHFS_COMP" -Xcompression-level 15 \
    -wildcards -e 'proc/*' 'sys/*' 'dev/*' 'run/*' 'tmp/*' 'var/cache/*' 'var/log/*' \
    >"$NULL_TARGET"

cp "$PROJECT_DIR/grub/grub.cfg" "$ISO_DIR/boot/grub/grub.cfg"

echo "[iso] grub-mkrescue (BIOS + UEFI) ..."
grub-mkrescue -o "$ISO" -R "$ISO_DIR"

echo "[iso] done: $ISO ($(du -h "$ISO" | cut -f1))"
echo "[iso] run:  qemu-system-x86_64 -cdrom $ISO -m 1024M"
