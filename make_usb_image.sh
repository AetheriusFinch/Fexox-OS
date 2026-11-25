#!/bin/bash
# Fexox OS Project, EULA GPLv2





















set -e
echo "=== Fexox OS: сборка build/usb.img (MBR: раздел 1 FAT32 + раздел 2 FHFS) ==="

command -v mformat >/dev/null || { echo "[!] mtools не найден (pacman -S mingw-w64-ucrt-x86_64-mtools)"; exit 1; }

for f in build/bootloader.efi build/kernel.bin; do
    [ -f "$f" ] || { echo "[!] $f не найден — сначала запустите Compile.sh"; exit 1; }
done

# Раскладка образа:
#   MBR | 1 МиБ зазор | раздел 1: FAT32 (UEFI-загрузка, kernel.bin, UI/APPS/init - "seed")
#                     | раздел 2: тип 0xDA, пустой - ядро само форматирует его в FHFS при первой загрузке
# USB_IMG_MB - размер FAT32-раздела (МиБ), FHFS_MB - размер раздела FHFS (МиБ).
# FHFS_MB=0 -> старый вариант: сырой FAT32 без таблицы разделов (FHFS тогда не используется).
USB_IMG_MB="${USB_IMG_MB:-128}"
FHFS_MB="${FHFS_MB:-512}"
PART=build/usb_part.img
IMG=build/usb.img

echo "[*] Раздел FAT32 (${USB_IMG_MB} МБ)..."
rm -f "$PART"
dd if=/dev/zero of="$PART" bs=1M count=0 seek="$USB_IMG_MB" status=none
mformat -i "$PART" -F -v FEXUSB ::
mmd -i "$PART" ::EFI ::EFI/BOOT

mcopy -i "$PART" build/bootloader.efi ::EFI/BOOT/BOOTX64.EFI
mcopy -i "$PART" build/kernel.bin      ::kernel.bin
echo "[OK] bootloader.efi -> ::EFI/BOOT/BOOTX64.EFI, kernel.bin -> ::kernel.bin"

# маркер "это загрузочный/seed-том": с него ядро копирует UI, APPS, init в FHFS
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
    echo "[OK] UI/ скопирована"
else
    echo "[!] Папка UI/ не найдена рядом со скриптом — обои/курсор/иконки не будут скопированы"
fi

if [ -d "APPS" ]; then
    echo "[*] Копирую APPS/ (манифесты .fxapp) ..."
    mmd -i "$PART" ::APPS 2>/dev/null || true
    for f in APPS/*; do
        [ -f "$f" ] || continue
        mcopy -i "$PART" "$f" "::APPS/$(basename "$f")"
    done
fi

mmd -i "$PART" ::APPS 2>/dev/null || true
[ -f init ]              && mcopy -i "$PART" init              ::init             && echo "[OK] init -> ::init"
[ -f build/mypc.elf ]    && mcopy -i "$PART" build/mypc.elf    ::APPS/mypc.elf    && echo "[OK] mypc.elf -> ::APPS/mypc.elf"
[ -f build/calc.elf ]    && mcopy -i "$PART" build/calc.elf    ::APPS/calc.elf    && echo "[OK] calc.elf -> ::APPS/calc.elf"
[ -f build/syslogs.elf ] && mcopy -i "$PART" build/syslogs.elf ::APPS/syslogs.elf && echo "[OK] syslogs.elf -> ::APPS/syslogs.elf"
[ -f build/browser.elf ]  && mcopy -i "$PART" build/browser.elf  ::APPS/browser.elf  && echo "[OK] browser.elf -> ::APPS/browser.elf"
[ -f build/musltest.elf ] && mcopy -i "$PART" build/musltest.elf ::APPS/musltest.elf && echo "[OK] musltest.elf -> ::APPS/musltest.elf"

if [ "$FHFS_MB" = "0" ]; then
    cp -f "$PART" "$IMG"
    echo "[!] FHFS_MB=0: образ без таблицы разделов, система будет работать с FAT32 как корнем"
else
    PART_START=2048
    PART_SECS=$(( USB_IMG_MB * 2048 ))
    FHFS_START=$(( PART_START + PART_SECS ))
    FHFS_SECS=$(( FHFS_MB * 2048 ))
    TOTAL_MB=$(( 1 + USB_IMG_MB + FHFS_MB ))

    le32() {
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $(( $1 & 255 )) $(( ($1 >> 8) & 255 )) $(( ($1 >> 16) & 255 )) $(( ($1 >> 24) & 255 )))"
    }

    echo "[*] Собираю $IMG (${TOTAL_MB} МБ): FAT32 + FHFS..."
    rm -f "$IMG"
    dd if=/dev/zero of="$IMG" bs=1M count=0 seek="$TOTAL_MB" status=none
    {
        head -c 446 /dev/zero
        # раздел 1: активный, тип 0xEF (EFI System) - так UEFI-прошивки грузятся с MBR-флешек
        printf '\x80\xfe\xff\xff\xef\xfe\xff\xff'; le32 $PART_START;  le32 $PART_SECS
        # раздел 2: тип 0xDA - Fexox (FHFS), ядро форматирует его при первой загрузке
        printf '\x00\xfe\xff\xff\xda\xfe\xff\xff'; le32 $FHFS_START; le32 $FHFS_SECS
        head -c 32 /dev/zero
        printf '\x55\xaa'
    } | dd of="$IMG" conv=notrunc status=none
    dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none
    echo "[OK] раздел 1: FAT32 ${USB_IMG_MB} МБ, раздел 2: FHFS ${FHFS_MB} МБ (тип 0xDA)"
fi
rm -f "$PART"

[ -n "$QUIET" ] && { echo "[+] Готово: $IMG"; exit 0; }
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
