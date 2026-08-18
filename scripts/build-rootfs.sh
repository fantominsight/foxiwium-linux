#!/bin/bash
# Foxiwium Linux — build the Debian-based root filesystem (live, console).
#
# ВЫПОЛНЯТЬ ТОЛЬКО И ТОЛЬКО В КОНТЕЙНЕР DOCKER, А ТО СНЕСЕШЬ МНЕ СИСТЕМУ!
# Uses debootstrap + apt inside a chroot, then applies the rootfs-overlay
# branding/config and regenerates the initramfs with live-boot.
#
# Environment:
#   FORCE=1            rebuild from scratch even if already built
#   SUITE=<name>       Debian suite (default: trixie)
#   MIRROR=<url>       Debian mirror (default: deb.debian.org)
#   ROOT_PASSWORD=...  password for root/fox (default: see ROADMAP)

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
ROOTFS="$PROJECT_DIR/rootfs"
OVERLAY="$PROJECT_DIR/rootfs-overlay"
MIRROR="${MIRROR:-http://deb.debian.org/debian}"
SUITE="${SUITE:-trixie}"
ARCH="amd64"
MARKER="$ROOTFS/.foxiwium-complete"
HOSTNAME="foxiwium"
USERNAME="fox"
ROOT_PASSWORD="${ROOT_PASSWORD:-breadyouroute4}"

# Packages for a comfortable CLI system
PACKAGES="
linux-image-amd64
initramfs-tools
live-boot
systemd systemd-sysv systemd-resolved
openssh-server openssh-client
vim nano less htop
curl wget iputils-ping iproute2 net-tools dnsutils
bash-completion man-db
sudo ca-certificates apt-utils
console-setup keyboard-configuration locales tzdata
"

# Desktop — KDE Plasma on X11 (Xorg). Installed in a separate step WITH
# Recommends so dolphin/konsole/kate/etc. come along (see STEP 3b).
#
# NOTE: there is NO "task-kde-plasma-desktop" package in Debian (that is an
# Ubuntu name); the Debian equivalent is the "kde-plasma-desktop" meta-package.
#
# Wayland is deliberately avoided: the target GPU (NVIDIA GeForce GT 210) only
# has usable acceleration on X11 via the nouveau driver, and the Plasma X11
# session (plasmax11) is made the only SDDM choice (see STEP 4b).
KDE_PACKAGES="
kde-plasma-desktop
sddm
xserver-xorg
xserver-xorg-video-nouveau
network-manager
fonts-noto-core
calamares
rsync
parted
grub-pc-bin
grub-efi-amd64-bin
efibootmgr
os-prober
"

say() { printf '\033[1;33m[rootfs]\033[0m %s\n' "$*"; }

if [[ -f "$MARKER" && "${FORCE:-0}" != "1" ]]; then
    say "already built ($MARKER); FORCE=1 to rebuild"
    exit 0
fi

# Make sure sudo credentials are cached before the long steps
sudo -v

# ---- Mount safety ---------------------------------------------------------
# Never delete $ROOTFS while /proc, /sys or /dev are bind-mounted inside it:
# rm -rf would descend into the mount and wipe the HOST's /dev, /sys, /proc.
#
# Two layers of defence:
#   1. redirect stderr to $NULL_TARGET (never hard-coded /dev/null: if /dev/null
#      is ever missing/wrong, a `2>/dev/null` redirect fails silently and the
#      umount below would NOT run — that is exactly how host /dev got wiped).
#   2. refuse to `rm -rf` unless NO mounts remain under $ROOTFS.

NULL_TARGET=/dev/null
[[ -w "$NULL_TARGET" ]] || NULL_TARGET=/tmp/foxiwium-null

# List the absolute target paths of every mount at or under $1. $1 itself need
# NOT be a mountpoint — that is the whole point: the dangerous mounts live
# UNDER the chroot dir (/dev, /sys, /proc), not at it.
#
# IMPORTANT: do NOT use `findmnt -R -n -o TARGET "$ROOTFS"` here. `-R`
# (--subtree) only lists mounts below a real MOUNTPOINT; when the argument is a
# plain directory (rootfs) it silently prints nothing (rc=1), the guard looks
# "empty", and the subsequent `rm -rf` descends into the bind mount and wipes
# the HOST's /dev. That is exactly how /dev got wiped three times.
mounts_under() {
    local root="$1"
    findmnt -n -r -o TARGET | awk -v p="$root" '$0 == p || index($0, p "/") == 1'
}

# Lazily unmount every mount under $ROOTFS (deepest first), retrying a few
# times; tolerant of already-gone mountpoint paths.
unmount_all() {
    for _ in 1 2 3 4 5; do
        local targets
        targets="$(mounts_under "$ROOTFS" | awk '{ print length, $0 }' | sort -rn | sed 's/^[0-9]* //')"
        [[ -z "$targets" ]] && break
        while read -r m; do
            sudo umount -l "$m" 2>"$NULL_TARGET" || true
        done <<< "$targets"
    done
}

remove_rootfs() {
    unmount_all
    local left
    left="$(mounts_under "$ROOTFS" | head -n 3)"
    if [[ -n "$left" ]]; then
        echo "[rootfs] FATAL: mounts still present under $ROOTFS:" >&2
        echo "$left" >&2
        echo "[rootfs] refusing to delete $ROOTFS" >&2
        exit 1
    fi
    sudo rm -rf "$ROOTFS"
}

cleanup_mounts() {
    unmount_all
    rm -f "${ASKPASS:-}"
}
trap cleanup_mounts EXIT

chroot_cmd() { sudo chroot "$ROOTFS" "$@"; }

say "STEP 1/6: debootstrap $SUITE/$ARCH ..."
remove_rootfs
sudo mkdir -p "$ROOTFS"
sudo debootstrap --arch="$ARCH" --variant=minbase \
    --include=apt-utils,ca-certificates,sudo,locales,tzdata,less \
    "$SUITE" "$ROOTFS" "$MIRROR"

say "STEP 2/6: mount helper filesystems and set apt sources ..."
sudo mount -t proc proc "$ROOTFS/proc"
sudo mount --rbind /dev "$ROOTFS/dev"
sudo mount --rbind /sys "$ROOTFS/sys"
sudo cp -a "$OVERLAY/etc/." "$ROOTFS/etc/"

say "STEP 3/6: apt update + install packages ..."
chroot_cmd apt-get update -qq
chroot_cmd env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    --no-install-recommends $PACKAGES

if [[ -n "${KDE_PACKAGES:-}" ]]; then
    say "STEP 3b/6: desktop (Plasma on Xorg, with Recommends) ..."
    chroot_cmd env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        $KDE_PACKAGES
fi

say "STEP 4/6: configure system (users, services, locales, initramfs) ..."
# Locale + timezone
printf 'Europe/Moscow\n' | sudo tee "$ROOTFS/etc/timezone" >"$NULL_TARGET"
sudo ln -sf /usr/share/zoneinfo/Europe/Moscow "$ROOTFS/etc/localtime"
chroot_cmd env DEBIAN_FRONTEND=noninteractive locale-gen

# Users
chroot_cmd bash -c "echo 'root:$ROOT_PASSWORD' | chpasswd"
chroot_cmd useradd -m -s /bin/bash -G sudo "$USERNAME" || true
chroot_cmd bash -c "echo '$USERNAME:$ROOT_PASSWORD' | chpasswd"

# Services
chroot_cmd systemctl enable ssh
chroot_cmd systemctl enable serial-getty@ttyS0.service || true
if [[ -n "${KDE_PACKAGES:-}" ]]; then
    # Desktop networking goes through NetworkManager (what Plasma's widget
    # expects); the overlay 10-eth.network is then inert.
    chroot_cmd systemctl disable systemd-networkd systemd-resolved || true
    chroot_cmd systemctl enable NetworkManager
    chroot_cmd systemctl enable sddm
    chroot_cmd systemctl set-default graphical.target

    say "STEP 4b/6: force X11 (no Wayland) ..."
    # The only SDDM session must be Plasma-X11 (plasmax11). Remove every
    # Wayland session entry so SDDM cannot even offer Wayland: the NVIDIA
    # GT 210 has no usable GL for it.
    sudo rm -rf "$ROOTFS/usr/share/wayland-sessions" || true
    sudo rm -f "$ROOTFS/usr/share/sddm/wayland-sessions/"*.desktop || true
else
    chroot_cmd systemctl enable systemd-networkd systemd-resolved
fi

# Regenerate initramfs with live-boot scripts + squashfs module
chroot_cmd update-initramfs -u -k all

say "STEP 5/6: apply branding overlay ..."
sudo cp -a "$OVERLAY/etc/." "$ROOTFS/etc/"
sudo cp -a "$OVERLAY/usr/." "$ROOTFS/usr/"
sudo chmod 0755 "$ROOTFS/usr/local/bin/fox-help"
sudo chmod 0644 "$ROOTFS/etc/profile.d/foxiwium.sh"
# Installer + autostart scripts and calamares helpers
sudo chmod 0755 "$ROOTFS/usr/bin/calamares-install-foxiwium" \
    "$ROOTFS/usr/bin/add-foxiwium-desktop-icon" \
    "$ROOTFS/usr/bin/foxiwium-autostart"
sudo chmod 0755 "$ROOTFS/usr/share/calamares/helpers/"calamares-foxiwium-*
sudo chmod 0644 "$ROOTFS/etc/xdg/autostart/foxiwium-installer.desktop" \
    "$ROOTFS/usr/share/applications/foxiwium-installer.desktop"
sudo chmod 0440 "$ROOTFS/etc/sudoers.d/foxiwium"

# `cp -a` copies the overlay straight from the host, so every file keeps the
# host uid (licheng = uid 1000). Inside the guest that uid belongs to the live
# user "fox", which would make polkitd/sudo/pam misread the files. Normalize
# every overlay file to root:root.
sudo find "$OVERLAY" \( -type f -o -type d \) -printf '%P\n' \
    | while IFS= read -r rel; do
        [[ -n "$rel" ]] || continue
        sudo chown root:root "$ROOTFS/$rel" 2>"$NULL_TARGET" || true
    done
# The polkit rules directory must stay readable by polkitd (root:polkitd 0750).
# Resolve polkitd's GID from the rootfs's own /etc/group: the build container
# does not have a polkitd group, only the chroot does.
if [[ -d "$ROOTFS/etc/polkit-1/rules.d" ]]; then
    polkitd_gid="$(awk -F: '/^polkitd:/{print $3}' "$ROOTFS/etc/group" 2>"$NULL_TARGET" || true)"
    if [[ -n "$polkitd_gid" ]]; then
        sudo chown "root:$polkitd_gid" "$ROOTFS/etc/polkit-1/rules.d"
        sudo chmod 0750 "$ROOTFS/etc/polkit-1/rules.d"
    fi
fi

say "STEP 6/6: cleanup and finalize ..."
chroot_cmd apt-get clean
sudo rm -rf "$ROOTFS/var/lib/apt/lists" "$ROOTFS/var/cache/apt/archives"
sudo rm -f "$ROOTFS/root/.bash_history"
sudo touch "$MARKER"
cleanup_mounts
trap - EXIT

say "done: $ROOTFS"
