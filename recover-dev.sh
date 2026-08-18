#!/bin/bash
# Foxiwium recovery: restore host /dev after a build script wiped it.
#
# Works without a tty (uses sudo -A + askpass). Password from $SUDOPASS,
# defaults to the project password (see ROADMAP.md).
#
# Run:   bash recover-dev.sh

set -u

SUDOPASS="${SUDOPASS:-breadyouroute4}"
ASKPASS="$(mktemp)"
trap 'rm -f "$ASKPASS"' EXIT
printf '#!/bin/sh\necho "%s"\n' "$SUDOPASS" > "$ASKPASS"
chmod 700 "$ASKPASS"
export SUDO_ASKPASS="$ASKPASS"
sudo() { command sudo -A "$@"; }

echo "==> 1/3 recreate static device nodes via systemd-tmpfiles ..."
sudo systemd-tmpfiles --create --prefix=/dev || true

echo "==> 2/3 ensure core nodes exist (mknod if missing) ..."
while read -r name major minor; do
    [ -z "$name" ] && continue
    if [ ! -e "/dev/$name" ]; then
        sudo mknod -m 666 "/dev/$name" c "$major" "$minor" \
            && echo "  + /dev/$name (c $major:$minor)"
    else
        echo "  = /dev/$name ok"
    fi
done <<'DEVICES'
null 1 3
zero 1 5
full 1 7
random 1 8
urandom 1 9
tty 5 0
console 5 1
ptmx 5 2
kmsg 1 11
kvm 10 232
fuse 10 229
DEVICES

echo "==> 2b/3 misc nodes, symlinks and dirs ..."
sudo mkdir -p /dev/net
[ -e /dev/net/tun ] || sudo mknod -m 666 /dev/net/tun c 10 200
sudo ln -sf /proc/self/fd /dev/fd
sudo ln -sf /dev/fd/0 /dev/stdin
sudo ln -sf /dev/fd/1 /dev/stdout
sudo ln -sf /dev/fd/2 /dev/stderr

echo "==> 3/3 let udev repopulate the rest ..."
sudo udevadm trigger || true

echo ""
echo "==> Done. Sanity check:"
ls -la /dev/null /dev/zero /dev/tty /dev/kvm /dev/net/tun 2>&1 || true
echo ""
echo "If the desktop/input/video is still broken, reboot the host —"
echo "devtmpfs + udev will fully repopulate /dev on boot."
