#!/bin/sh
# Foxiwium Linux — quick start
set -e
make iso
exec sudo qemu-system-x86_64 -cdrom build/foxiwium.iso -m 2048M -cpu max -smp 2 \
    -accel kvm -nic user -display sdl
