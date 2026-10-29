#!/bin/bash
# Fexox OS Project, EULA GPLv2





















set -e
echo "=== Fexox OS: сборка build/usb.img (сырой FAT32, без MBR/GPT) ==="

command -v mformat >/dev/null || { echo "[!] mtools не найден (pacman -S mingw-w64-ucrt-x86_64-mtools)"; exit 1; }

for f in build/bootloader.efi build/kernel.bin; do
    [ -f "$f" ] || { echo "[!] $f не найден — сначала запустите Compile.sh"; exit 1; }
done




USB_IMG_MB="${USB_IMG_MB:-256}"

echo "[*] Создаю build/usb.img (${USB_IMG_MB} МБ)..."
dd if=/dev/zero of=build/usb.img bs=1M count="$USB_IMG_MB" status=none
mformat -i build/usb.img -F -v FEXUSB ::
mmd -i build/usb.img ::EFI ::EFI/BOOT

mcopy -i build/usb.img build/bootloader.efi ::EFI/BOOT/BOOTX64.EFI
mcopy -i build/usb.img build/kernel.bin      ::kernel.bin
echo "[OK] bootloader.efi -> ::EFI/BOOT/BOOTX64.EFI, kernel.bin -> ::kernel.bin"



printf 'fs0:\n\\EFI\\BOOT\\BOOTX64.EFI\n' > build/startup.nsh
mcopy -i build/usb.img build/startup.nsh ::startup.nsh

if [ -d "UI" ]; then
    echo "[*] Копирую UI/ ..."
    mmd -i build/usb.img ::UI 2>/dev/null || true
    for f in UI/*; do
        [ -f "$f" ] || continue
        mcopy -i build/usb.img "$f" "::UI/$(basename "$f")"
    done
    echo "[OK] UI/ скопирована"
else
    echo "[!] Папка UI/ не найдена рядом со скриптом — обои/курсор/иконки не будут скопированы"
fi

if [ -d "APPS" ]; then
    echo "[*] Копирую APPS/ (манифесты .fxapp) ..."
    mmd -i build/usb.img ::APPS 2>/dev/null || true
    for f in APPS/*; do
        [ -f "$f" ] || continue
        mcopy -i build/usb.img "$f" "::APPS/$(basename "$f")"
    done
fi

mmd -i build/usb.img ::APPS 2>/dev/null || true
[ -f build/calc.elf ]    && mcopy -i build/usb.img build/calc.elf    ::APPS/calc.elf    && echo "[OK] calc.elf -> ::APPS/calc.elf"
[ -f build/syslogs.elf ] && mcopy -i build/usb.img build/syslogs.elf ::APPS/syslogs.elf && echo "[OK] syslogs.elf -> ::APPS/syslogs.elf"
[ -f build/browser.elf ]  && mcopy -i build/usb.img build/browser.elf  ::APPS/browser.elf  && echo "[OK] browser.elf -> ::APPS/browser.elf"

echo ""
echo "[+] Готово: build/usb.img"
echo ""
echo "=== Дальше — ЗАПИСЬ НА ФЛЕШКУ (сделайте вручную, внимательно) ==="
echo ""
echo "Windows (Win32DiskImager или Rufus):"
echo "  1. Win32DiskImager -> Image File: build/usb.img -> выбрать букву диска"
echo "     ИМЕННО ФЛЕШКИ (проверьте по размеру в 'Этот компьютер'!) -> Write."
echo "  2. Либо Rufus: Device = ваша флешка, 'Select' -> build/usb.img,"
echo "     Image Option ДОЛЖЕН переключиться в 'Write in DD Image mode'"
echo "     (это важно — иначе Rufus начнёт партиционировать заново)."
echo ""
echo "Linux/macOS:"
echo "  1. lsblk (Linux) или diskutil list (macOS) — найдите СВОЮ флешку"
echo "     ТОЛЬКО по размеру/модели. НЕ /dev/sda если это не флешка!"
echo "  2. sudo dd if=build/usb.img of=/dev/sdX bs=4M status=progress conv=fsync"
echo "     (macOS: of=/dev/rdiskN, диск сначала diskutil unmountDisk /dev/diskN)"
echo ""
echo "После записи в BIOS/UEFI выберите загрузку с флешки через boot menu"
echo "(F12/F11/Esc/F8 при включении — зависит от материнки), НЕ меняя"
echo "постоянный порядок загрузки в настройках — тогда после извлечения"
echo "флешки ПК как ни в чём не бывало загрузится со своего штатного диска."
