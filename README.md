# Foxiwium Linux

**Настоящий Linux**: Debian-based live-ISO с **KDE Plasma на X11** и
**графическим установщиком Calamares**. Собирается через `debootstrap`,
`initramfs-tools` + `live-boot` и `grub-mkrescue` (GRUB BIOS+UEFI).

## Быстрый старт

```bash
make iso-in-docker       # собрать build/foxiwium.iso (ВСЁ в Docker)
make run                 # запустить live в QEMU (окно)
make run-nographic       # запустить в QEMU (serial console)
make deepclean           # удалить build/ и rootfs/
```

**ПРЕДУПРЕЖДЕНИЕ**: скрипты сборки НИКОГДА не запускать на хосте — они
bind-mount'ят /dev,/sys,/proc внутрь `rootfs/`, и `rm -rf` может стереть
host `/dev` (уже случалось). Сборка — только в Docker: `make iso-in-docker`
(контейнер настраивается сам через `make docker-builder`).

## Графика

- **KDE Plasma 6 на X11** (Xorg), дисплейный менеджер **SDDM** (autologin в
  live-сессии). Wayland отключён намеренно.
- NVIDIA GeForce GT 210 (и аналоги) работает через открытый драйвер
  **nouveau** (конфиг: `rootfs-overlay/etc/X11/xorg.conf.d/20-foxiwium-video.conf`).
  Проприетарный nvidia-legacy драйвер для GT 210 EOL и в Debian отсутствует.

## Установка на компьютер / в VM

- В меню GRUB выбрать **"Foxiwium Linux — install to hard disk"** (или после
  загрузки live кликнуть иконку **"Install Foxiwium Linux"** на рабочем столе).
- Откроется графический мастер **Calamares**: язык → раскладка → разметка
  диска (erase / manual) → пользователь и пароль root → установка → GRUB
  (BIOS и UEFI) → перезагрузка.

## Как это устроено

- `scripts/build-rootfs.sh` — debootstrap (trixie) в `rootfs/`, установка
  пакетов (Plasma, SDDM, Xorg, nouveau, Calamares), initramfs с live-boot.
- `rootfs-overlay/` — конфиги и брендинг гостя: sources.list, networkd,
  sshd, console, os-release, motd, fox-help, SDDM/X11, установщик
  (`etc/calamares/`), иконка установщика, polkit-правило.
- `scripts/create-iso.sh` — squashfs корневой ФС + GRUB ISO (`/live/vmlinuz`,
  `/live/initrd.img`, `/live/filesystem.squashfs`, загрузка с `boot=live`).
- `grub/grub.cfg` — меню (live, install, serial, verbose).
- `build-env/` — Docker-контейнер сборки + `run-in-container.sh`.

## Требования (хост)

Docker (для сборки), `qemu-system-x86_64` (для запуска). Сеть нужна для
debootstrap/apt внутри контейнера.

## Переменные окружения

- `FORCE=1` — пересобрать rootfs заново.
- `SUITE=bookworm` — другой suite Debian.
- `MIRROR=...` — другой зеркальный сервер.
- `ROOT_PASSWORD=...` — пароли `root`/`fox`.
- `SQUASHFS_COMP=xz` — другой алгоритм сжатия squashfs.
