#!/bin/bash
# Foxiwium Linux — run a command inside the build container (Docker).
#
# WHY: every build script MUST run in a container. On the host, the bind-mounted
# /dev,/sys,/proc inside rootfs/ has wiped the host /dev three times.
#
# Usage:
#   build-env/run-in-container.sh <cmd...>
#   build-env/run-in-container.sh make iso          # full build
#   build-env/run-in-container.sh bash              # interactive shell in container
#
# Uses --network=host (the default docker bridge has no outbound net here)
# and --privileged (build-rootfs.sh bind-mounts /dev,/sys,/proc inside rootfs).

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="foxiwium-builder"

# Add -t only when stdin is a TTY (so backgrounded/non-interactive builds work).
TTY=""
if [ -t 0 ]; then
    TTY="-t"
fi

docker run --rm -i $TTY --network=host --privileged \
    -v "$REPO":/build:rw \
    -w /build \
    -e FORCE \
    -e SUITE \
    -e MIRROR \
    -e ROOT_PASSWORD \
    -e SQUASHFS_COMP \
    "$IMAGE" "$@"
