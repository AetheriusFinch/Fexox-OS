#!/bin/bash


set -e
echo "=== Fexox OS: creating build/usb.img (MBR: FAT32 partition + FHFS partition) ==="

command -v mformat >/dev/null || { echo "[!] mtools not found (install mingw-w64-ucrt-x86_64-mtools with pacman)"; exit 1; }

for f in build/bootloader.efi build/kernel.bin; do
    [ -f "$f" ] || { echo "[!] $f not found. Run Compile.sh first."; exit 1; }
done

USB_IMG_MB="${USB_IMG_MB:-128}"
FHFS_MB="${FHFS_MB:-512}"
PART=build/usb_part.img
IMG=build/usb.img

echo "[*] Creating FAT32 partition (${USB_IMG_MB} MB)..."
rm -f "$PART"
dd if=/dev/zero of="$PART" bs=1M count=0 seek="$USB_IMG_MB" status=none
mformat -i "$PART" -F -v FEXUSB ::
mmd -i "$PART" ::EFI ::EFI/BOOT

mcopy -i "$PART" build/bootloader.efi ::EFI/BOOT/BOOTX64.EFI
mcopy -i "$PART" build/kernel.bin      ::kernel.bin
echo "[OK] Copied bootloader.efi and kernel.bin to the FAT32 partition."

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
    echo "[OK] Copied UI/."
else
    echo "[!] UI/ was not found next to the script. Wallpapers, cursor, and icons will not be copied."
fi

if [ -d "APPS" ]; then
    echo "[*] Copying APPS/ (.fxapp manifests)..."
    mmd -i "$PART" ::APPS 2>/dev/null || true
    for f in APPS/*; do
        [ -f "$f" ] || continue
        mcopy -i "$PART" "$f" "::APPS/$(basename "$f")"
    done
fi

mmd -i "$PART" ::APPS 2>/dev/null || true
[ -f init ]              && mcopy -i "$PART" init              ::init             && echo "[OK] init -> ::init"
[ -f build/calc.elf ]    && mcopy -i "$PART" build/calc.elf    ::APPS/calc.elf    && echo "[OK] calc.elf -> ::APPS/calc.elf"
[ -f build/syslogs.elf ] && mcopy -i "$PART" build/syslogs.elf ::APPS/syslogs.elf && echo "[OK] syslogs.elf -> ::APPS/syslogs.elf"
[ -f build/browser.elf ]  && mcopy -i "$PART" build/browser.elf  ::APPS/browser.elf  && echo "[OK] browser.elf -> ::APPS/browser.elf"
[ -f build/musltest.elf ] && mcopy -i "$PART" build/musltest.elf ::APPS/musltest.elf && echo "[OK] musltest.elf -> ::APPS/musltest.elf"

if [ "$FHFS_MB" = "0" ]; then
    cp -f "$PART" "$IMG"
    echo "[!] FHFS_MB=0: created an image without a partition table. The system will use FAT32 as its root filesystem."
else
    PART_START=2048
    PART_SECS=$(( USB_IMG_MB * 2048 ))
    FHFS_START=$(( PART_START + PART_SECS ))
    FHFS_SECS=$(( FHFS_MB * 2048 ))
    TOTAL_MB=$(( 1 + USB_IMG_MB + FHFS_MB ))

    le32() {
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $(( $1 & 255 )) $(( ($1 >> 8) & 255 )) $(( ($1 >> 16) & 255 )) $(( ($1 >> 24) & 255 )))"
    }

    echo "[*] Creating $IMG (${TOTAL_MB} MB): FAT32 + FHFS..."
    rm -f "$IMG"
    dd if=/dev/zero of="$IMG" bs=1M count=0 seek="$TOTAL_MB" status=none
    {
        head -c 446 /dev/zero
        printf '\x80\xfe\xff\xff\xef\xfe\xff\xff'; le32 $PART_START;  le32 $PART_SECS
        printf '\x00\xfe\xff\xff\xda\xfe\xff\xff'; le32 $FHFS_START; le32 $FHFS_SECS
        head -c 32 /dev/zero
        printf '\x55\xaa'
    } | dd of="$IMG" conv=notrunc status=none
    dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none
    echo "[OK] Partition 1: FAT32 ${USB_IMG_MB} MB; partition 2: FHFS ${FHFS_MB} MB (type 0xDA)."
fi
rm -f "$PART"

[ -n "$QUIET" ] && { echo "[+] Done: $IMG"; exit 0; }
echo ""
echo "[+] Image created: build/usb.img"
echo ""
echo "=== Flash the image to a USB drive with balenaEtcher ==="
echo ""
echo "  1. Open balenaEtcher and choose 'Flash from file'."
echo "  2. Select build/usb.img."
echo "  3. Choose the USB drive as the target and click 'Flash'."
echo "     Verify the drive by its model and capacity. Flashing erases its contents."
echo ""
echo "After flashing, select the USB drive from the BIOS/UEFI boot menu."
