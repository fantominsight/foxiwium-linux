# Foxiwium Linux — build system (real Linux, Debian-based live ISO)
#
# !!! ВАЖНО !!!
#   rootfs собирается с bind-mount'ами /dev,/sys,/proc внутри rootfs/.
#   Запуск scripts/build-rootfs.sh и scripts/create-iso.sh НА ХОСТЕ может
#   стереть host /dev (это уже случалось). ВСЕГДА собирай в Docker:
#
#       make iso-in-docker        # rootfs + ISO внутри контейнера
#       make rootfs-in-docker     # только rootfs
#
# Targets:
#   make iso-in-docker    build build/foxiwium.iso entirely inside Docker
#   make rootfs-in-docker rebuild the rootfs inside Docker
#   make iso              build the ISO from the existing rootfs (HOST, DANGEROUS)
#   make run              run the ISO in QEMU (GUI window)
#   make run-nographic    run in QEMU on the serial console (no window)
#   make clean            remove build artifacts (not the rootfs)
#   make deepclean        remove build artifacts AND the rootfs
#   make docker-builder   (re)build the Docker build image

BUILDDIR := build
ROOTFS   := rootfs
ISO      := $(BUILDDIR)/foxiwium.iso

QEMU     := qemu-system-x86_64
QEMUFLAGS := -m 2048M -cpu max -smp 2 -accel kvm -nic user -display sdl

CONTAINER := build-env/run-in-container.sh

.PHONY: all rootfs iso run run-nographic clean deepclean info \
        iso-in-docker rootfs-in-docker docker-builder

all: iso-in-docker
	@echo ""
	@echo "==========================================================="
	@echo "  [done] Bootable live ISO: $(ISO)"
	@echo "  Run in QEMU:  qemu-system-x86_64 -cdrom $(ISO) -m 1024M"
	@echo "==========================================================="

docker-builder:
	@docker build --network=host -t foxiwium-builder build-env

rootfs-in-docker:
	@$(CONTAINER) env FORCE=1 bash scripts/build-rootfs.sh

iso-in-docker: docker-builder
	@$(CONTAINER) bash scripts/build-rootfs.sh
	@$(CONTAINER) bash scripts/create-iso.sh

rootfs:
	@./scripts/build-rootfs.sh

iso: $(ISO)

$(ISO): rootfs
	@./scripts/create-iso.sh

run:
	@test -f $(ISO) || { echo "ISO missing — run 'make iso' first"; exit 1; }
	$(QEMU) -cdrom $(ISO) $(QEMUFLAGS)

run-nographic:
	@test -f $(ISO) || { echo "ISO missing — run 'make iso' first"; exit 1; }
	$(QEMU) -cdrom $(ISO) -m 2048M -cpu max -smp 2 -accel kvm -nic user \
		-nographic -serial stdio

clean:
	rm -rf $(BUILDDIR)

deepclean: clean
	@sudo sh -c 'for m in sys dev proc; do umount -R rootfs/$$m 2>/dev/null; done; \
	root="$(CURDIR)/rootfs"; \
	left="$$(findmnt -n -r -o TARGET | awk -v p="$$root" '\''$$0 == p || index($$0, p "/") == 1'\'' | head -n 1)"; \
	if [ -n "$$left" ]; then echo "FATAL: $$left still mounted under rootfs, refusing rm -rf" >&2; exit 1; fi; \
	rm -rf "$$root"'

info:
	@echo "=== Foxiwium Linux ==="
	@echo "Suite:    trixie (set SUITE= to override)"
	@echo "Rootfs:   $(ROOTFS)/"
	@echo "ISO:      $(ISO)"
	@echo "Overlay:  rootfs-overlay/"
	@echo "QEMU:     $(QEMU)"
