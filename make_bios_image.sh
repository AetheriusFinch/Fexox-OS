#!/bin/bash


set -e
echo "=== Fexox OS: creating build/usb_bios.img (MBR + FAT32 + FHFS, BIOS and UEFI) ==="

command -v mformat >/dev/null || { echo "[!] mtools not found (install mingw-w64-ucrt-x86_64-mtools with pacman)"; exit 1; }

for f in build/bios/stage1.bin build/bios/stage2.bin build/kernel.bin build/bootloader.efi; do
    [ -f "$f" ] || { echo "[!] $f not found. Run Compile.sh and build_bios.sh first."; exit 1; }
done


PART_MB="${PART_MB:-128}"
FHFS_MB="${FHFS_MB:-512}"
PART_START=2048
PART_SECS=$(( PART_MB * 2048 ))
PART=build/bios_part.img
IMG=build/usb_bios.img


echo "[*] Creating FAT32 partition (${PART_MB} MB)..."
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
    echo "[*] Copying UI/..."
    mmd -i "$PART" ::UI 2>/dev/null || true
    for f in UI/*; do
        [ -f "$f" ] || continue
        mcopy -i "$PART" "$f" "::UI/$(basename "$f")"
    done
else
    echo "[!] UI/ was not found. Wallpapers, cursor, and icons will not be copied."
fi

mmd -i "$PART" ::APPS 2>/dev/null || true
if [ -d "APPS" ]; then
    echo "[*] Copying APPS/ (.fxapp manifests)..."
    for f in APPS/*; do
        [ -f "$f" ] || continue
        mcopy -i "$PART" "$f" "::APPS/$(basename "$f")"
    done
fi
[ -f init ]              && mcopy -i "$PART" init              ::init
[ -f build/calc.elf ]    && mcopy -i "$PART" build/calc.elf    ::APPS/calc.elf
[ -f build/syslogs.elf ] && mcopy -i "$PART" build/syslogs.elf ::APPS/syslogs.elf
[ -f build/browser.elf ] && mcopy -i "$PART" build/browser.elf ::APPS/browser.elf


echo "[*] Creating $IMG..."
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
printf "\\x00\\xfe\\xff\\xff\\xda\\xfe\\xff\\xff$(le32 $FHFS_START)$(le32 $FHFS_SECS)" \
    | dd of="$IMG" bs=1 seek=462 conv=notrunc status=none
printf '\x55\xaa' | dd of="$IMG" bs=1 seek=510 conv=notrunc status=none


dd if=build/bios/stage2.bin of="$IMG" bs=512 seek=1 conv=notrunc status=none


dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none

[ -n "$QUIET" ] && { echo "[+] Done: $IMG"; exit 0; }
echo ""
echo "[+] Done: $IMG"
echo ""
echo "Test in QEMU using legacy BIOS (SeaBIOS by default); the image is attached as a USB drive:"
echo "  qemu-system-x86_64 -m 512 -serial stdio -device usb-ehci,id=ehci \\"
echo "    -drive if=none,id=usbd,file=$IMG,format=raw \\"
echo "    -device usb-storage,bus=ehci.0,drive=usbd,bootindex=0"
echo "Or attach it as a VirtIO drive (the kernel will mount the partition):"
echo "  qemu-system-x86_64 -m 512 -serial stdio -drive file=$IMG,if=virtio,format=raw"
echo ""
echo "Flash this image to a USB drive with balenaEtcher:"
echo "  1. Choose 'Flash from file' and select $IMG."
echo "  2. Select the USB drive as the target and click 'Flash'."
echo "     Verify the drive by its model and capacity. Flashing erases its contents."
echo "After flashing, select the USB drive from the BIOS/UEFI boot menu."
