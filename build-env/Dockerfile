FROM debian:trixie-slim

RUN echo "deb http://deb.debian.org/debian trixie main\ndeb http://deb.debian.org/debian trixie-updates main\ndeb http://deb.debian.org/debian-security trixie-security main" > /etc/apt/sources.list \
    && apt-get update -qq \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    debootstrap \
    squashfs-tools \
    xorriso \
    grub-pc-bin \
    grub-efi-amd64-bin \
    mtools \
    sudo \
    ca-certificates \
    curl \
    cpio \
    binutils \
    util-linux \
    && rm -rf /var/lib/apt/lists/*
