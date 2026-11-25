#!/bin/bash
# Fexox OS Project, EULA GPLv2














set -e
echo "=== Fexox OS: сборка build/usb_bios.img (MBR + FAT32 + FHFS, BIOS и UEFI) ==="

command -v mformat >/dev/null || { echo "[!] mtools не найден (pacman -S mingw-w64-ucrt-x86_64-mtools)"; exit 1; }

for f in build/bios/stage1.bin build/bios/stage2.bin build/kernel.bin build/bootloader.efi; do
    [ -f "$f" ] || { echo "[!] $f не найден — запустите Compile.sh и build_bios.sh"; exit 1; }
done


PART_MB="${PART_MB:-128}"
FHFS_MB="${FHFS_MB:-512}"      # раздел 2 (тип 0xDA): ядро само форматирует его в FHFS при первой загрузке
PART_START=2048
PART_SECS=$(( PART_MB * 2048 ))
PART=build/bios_part.img
IMG=build/usb_bios.img


echo "[*] Раздел FAT32 (${PART_MB} МБ)..."
dd if=/dev/zero of="$PART" bs=1M count="$PART_MB" status=none
mformat -i "$PART" -F -v FEXUSB ::
mmd -i "$PART" ::EFI ::EFI/BOOT
mcopy -i "$PART" build/bootloader.efi ::EFI/BOOT/BOOTX64.EFI
mcopy -i "$PART" build/kernel.bin      ::kernel.bin
echo "Fexox OS seed volume" > build/FEXOX.SEED
mcopy -i "$PART" build/FEXOX.SEED ::FEXOX.SEED
printf 'fs0:\n\\EFI\\BOOT\\BOOTX64.EFI\n' > build/startup.nsh
mcopy -i "$PART" build/startup.nsh ::startup.nsh

if [ -d "UI" ]; then
    echo "[*] Копирую UI/ ..."
    mmd -i "$PART" ::UI 2>/dev/null || true
    for f in UI/*; do
        [ -f "$f" ] || continue
        mcopy -i "$PART" "$f" "::UI/$(basename "$f")"
    done
else
    echo "[!] Папка UI/ не найдена — обои/курсор/иконки не будут скопированы"
fi

mmd -i "$PART" ::APPS 2>/dev/null || true
if [ -d "APPS" ]; then
    echo "[*] Копирую APPS/ (манифесты .fxapp) ..."
    for f in APPS/*; do
        [ -f "$f" ] || continue
        mcopy -i "$PART" "$f" "::APPS/$(basename "$f")"
    done
fi
[ -f init ]              && mcopy -i "$PART" init              ::init
[ -f build/mypc.elf ]    && mcopy -i "$PART" build/mypc.elf    ::APPS/mypc.elf
[ -f build/calc.elf ]    && mcopy -i "$PART" build/calc.elf    ::APPS/calc.elf
[ -f build/syslogs.elf ] && mcopy -i "$PART" build/syslogs.elf ::APPS/syslogs.elf
[ -f build/browser.elf ] && mcopy -i "$PART" build/browser.elf ::APPS/browser.elf


echo "[*] Собираю $IMG ..."
FHFS_START=$(( PART_START + PART_SECS ))
FHFS_SECS=$(( FHFS_MB * 2048 ))
dd if=/dev/zero of="$IMG" bs=1M count=0 seek=$(( PART_MB + 1 + FHFS_MB )) status=none


dd if=build/bios/stage1.bin of="$IMG" bs=446 count=1 conv=notrunc status=none

le32() {
    printf '\\x%02x\\x%02x\\x%02x\\x%02x' \
        $(( $1 & 255 )) $(( ($1 >> 8) & 255 )) $(( ($1 >> 16) & 255 )) $(( ($1 >> 24) & 255 ))
}

printf "\\x80\\xfe\\xff\\xff\\xef\\xfe\\xff\\xff$(le32 $PART_START)$(le32 $PART_SECS)" \
    | dd of="$IMG" bs=1 seek=446 conv=notrunc status=none
# раздел 2: тип 0xDA (FHFS), не активный
printf "\\x00\\xfe\\xff\\xff\\xda\\xfe\\xff\\xff$(le32 $FHFS_START)$(le32 $FHFS_SECS)" \
    | dd of="$IMG" bs=1 seek=462 conv=notrunc status=none
printf '\x55\xaa' | dd of="$IMG" bs=1 seek=510 conv=notrunc status=none


dd if=build/bios/stage2.bin of="$IMG" bs=512 seek=1 conv=notrunc status=none


dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none

[ -n "$QUIET" ] && { echo "[+] Готово: $IMG"; exit 0; }
echo ""
echo "[+] Готово: $IMG"
echo ""
echo "Проверка в QEMU (legacy BIOS = SeaBIOS по умолчанию, ядро видит образ как USB-диск):"
echo "  qemu-system-x86_64 -m 512 -serial stdio -device usb-ehci,id=ehci \\"
echo "    -drive if=none,id=usbd,file=$IMG,format=raw \\"
echo "    -device usb-storage,bus=ehci.0,drive=usbd,bootindex=0"
echo "Или как virtio-диск (ядро смонтирует раздел само):"
echo "  qemu-system-x86_64 -m 512 -serial stdio -drive file=$IMG,if=virtio,format=raw"
echo ""
echo "Запись на флешку (ВНИМАТЕЛЬНО выбирайте устройство!):"
echo "  Windows: Win32DiskImager или Rufus в режиме 'DD Image' -> $IMG"
echo "  Linux:   sudo dd if=$IMG of=/dev/sdX bs=4M status=progress conv=fsync"
echo "В BIOS/UEFI выберите загрузку с флешки через boot menu (для legacy-режима"
echo "выберите пункт 'USB HDD' / без слова UEFI). Постоянный порядок загрузки не менять."
