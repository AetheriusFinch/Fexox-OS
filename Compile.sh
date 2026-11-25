#!/bin/bash
# Fexox OS Project, EULA GPLv2
set -e
echo "=== Fexox OS Build Script (MSYS2 UCRT64) ==="

CC="clang"
LD_KERN="ld.lld"

command -v $CC >/dev/null || { echo "[!] clang не найден"; exit 1; }

mkdir -p build

CFLAGS_KERN="-I. -IResources -IGUI -target x86_64-unknown-none-elf -ffreestanding -fno-builtin -mno-red-zone -mcmodel=kernel -fno-pie -fno-stack-protector -mgeneral-regs-only -O0 -g -Wall -Wextra -std=c11"
LDFLAGS_KERN="-m elf_x86_64 -nostdlib -T kernel.ld"








CFLAGS_USER="-target x86_64-unknown-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -O0 -g -Wall -Wextra -std=c11"
LDFLAGS_USER="-m elf_x86_64 -static -nostdlib -e _start"

MGW="x86_64-w64-mingw32-gcc"
command -v $MGW >/dev/null || { echo "[!] $MGW не найден"; exit 1; }

CFLAGS_BOOT="-I. -ffreestanding -fno-builtin -mno-red-zone -fno-stack-check -fno-stack-protector -mno-stack-arg-probe -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -O0 -g -std=c11"
LDFLAGS_BOOT="-nostdlib -nodefaultlibs -Wl,--subsystem,10 -Wl,--image-base=0x10000000 -Wl,--gc-sections -Wl,--emit-relocs -e efi_main"

echo "[*] Ядро..."
$CC $CFLAGS_KERN -c boot_stub.c        -o build/boot_stub.o
$CC $CFLAGS_KERN -c main.c             -o build/main.o
$CC $CFLAGS_KERN -c MemoryControl.c    -o build/MemoryControl.o
$CC $CFLAGS_KERN -c InterruptControl.c -o build/InterruptControl.o
$CC $CFLAGS_KERN -c Scheduler.c        -o build/Scheduler.o
$CC $CFLAGS_KERN -c Syscall.c          -o build/Syscall.o
$CC $CFLAGS_KERN -c VFS.c              -o build/VFS.o
$CC $CFLAGS_KERN -c FAT32.c            -o build/FAT32.o
# FHFS считает CRC и сжимает на каждом блоке - при -O0 это в разы медленнее; оптимизируем только его
$CC ${CFLAGS_KERN/-O0/-O2} -c FexoxHybridFS.c   -o build/FexoxHybridFS.o
$CC $CFLAGS_KERN -c Storage.c          -o build/Storage.o
$CC $CFLAGS_KERN -c IDE.c              -o build/IDE.o
$CC $CFLAGS_KERN -c AHCI.c             -o build/AHCI.o
$CC $CFLAGS_KERN -c NVMe.c             -o build/NVMe.o
$CC $CFLAGS_KERN -c klibc.c            -o build/klibc.o
$CC $CFLAGS_KERN -c ELF64.c            -o build/ELF64.o
$CC $CFLAGS_KERN -c GUI/Framebuffer.c  -o build/Framebuffer.o
# FexoxDriverDisplay: software scaler/rotator, hot pixel loops -> -O2
$CC ${CFLAGS_KERN/-O0/-O2} -c GUI/FexoxDriverDisplay.c -o build/FexoxDriverDisplay.o
$CC $CFLAGS_KERN -c png.c              -o build/png.o
$CC ${CFLAGS_KERN/-O0/-O2} -c jpeg.c  -o build/jpeg.o      # JPEG decoder: hot loops -> -O2
$CC $CFLAGS_KERN -c GUI/ui_extra.c     -o build/ui_extra.o
$CC ${CFLAGS_KERN/-O0/-O2} -c GUI/BootSplash.c   -o build/BootSplash.o
$CC $CFLAGS_KERN -c PS2Mouse.c         -o build/PS2Mouse.o
$CC $CFLAGS_KERN -c AcpiControl.c      -o build/AcpiControl.o
$CC $CFLAGS_KERN -c Resources/fxapp.c  -o build/fxapp.o
$CC $CFLAGS_KERN -c Resources/USB.c    -o build/USB.o
$CC $CFLAGS_KERN -c Resources/UsbDiag.c -o build/UsbDiag.o
$CC $CFLAGS_KERN -c Resources/xHCI.c  -o build/xHCI.o
$CC $CFLAGS_KERN -c Resources/UHCI.c   -o build/UHCI.o
$CC $CFLAGS_KERN -c Resources/EHCI.c   -o build/EHCI.o
$CC $CFLAGS_KERN -c Resources/USBHID.c         -o build/USBHID.o
$CC $CFLAGS_KERN -c Resources/USBMassStorage.c -o build/USBMassStorage.o
$CC $CFLAGS_KERN -c Resources/Network/net_core.c  -o build/net_core.o
$CC $CFLAGS_KERN -c Resources/Network/net_port.c  -o build/net_port.o
$CC $CFLAGS_KERN -c Resources/Network/eth_arp.c   -o build/eth_arp.o
$CC $CFLAGS_KERN -c Resources/Network/ip.c        -o build/ip.o
$CC $CFLAGS_KERN -c Resources/Network/udp.c       -o build/udp.o
$CC $CFLAGS_KERN -c Resources/Network/dhcp.c      -o build/dhcp.o
$CC $CFLAGS_KERN -c Resources/Network/dns.c       -o build/dns.o
$CC $CFLAGS_KERN -c Resources/Network/tcp.c       -o build/tcp.o
$CC $CFLAGS_KERN -c Resources/Network/socket.c    -o build/socket.o
$CC $CFLAGS_KERN -c Resources/Network/netdiag.c   -o build/netdiag.o
$CC $CFLAGS_KERN -c Resources/Network/usbnet.c    -o build/usbnet.o
$CC $CFLAGS_KERN -c Resources/Network/rndis.c     -o build/rndis.o
$CC $CFLAGS_KERN -c Resources/Network/cdc_ecm.c   -o build/cdc_ecm.o
$CC $CFLAGS_KERN -c Resources/Network/cdc_ncm.c   -o build/cdc_ncm.o

$LD_KERN $LDFLAGS_KERN -o build/kernel.bin \
    build/boot_stub.o      \
    build/main.o           \
    build/MemoryControl.o  \
    build/InterruptControl.o \
    build/Scheduler.o      \
    build/Syscall.o        \
    build/VFS.o            \
    build/FAT32.o          \
    build/FexoxHybridFS.o  \
    build/Storage.o        \
    build/IDE.o            \
    build/AHCI.o           \
    build/NVMe.o           \
    build/klibc.o          \
    build/ELF64.o          \
    build/Framebuffer.o    \
    build/FexoxDriverDisplay.o \
    build/png.o            \
    build/jpeg.o           \
    build/ui_extra.o        \
    build/BootSplash.o      \
    build/PS2Mouse.o        \
	build/AcpiControl.o     \
	build/fxapp.o           \
    build/USB.o             \
    build/UsbDiag.o         \
    build/xHCI.o            \
    build/UHCI.o            \
    build/EHCI.o             \
    build/USBHID.o          \
    build/USBMassStorage.o  \
    build/net_core.o        \
    build/net_port.o        \
    build/eth_arp.o         \
    build/ip.o              \
    build/udp.o             \
    build/dhcp.o            \
    build/dns.o             \
    build/tcp.o             \
    build/socket.o          \
    build/netdiag.o         \
    build/usbnet.o          \
    build/rndis.o           \
    build/cdc_ecm.o         \
    build/cdc_ncm.o

                                                                         
MUSL_OUT="Resources/musl/out"
CFLAGS_MUSL="-target x86_64-linux-musl -nostdinc -isystem $MUSL_OUT/include -fno-stack-protector -fno-pic -fno-pie -O2 -g -Wall -std=gnu11"
LDFLAGS_MUSL="-m elf_x86_64 -static -nostdlib -z max-page-size=4096 -z separate-code -z norelro"

rm -f build/calc.elf
if [ -f "$MUSL_OUT/lib/libc.a" ]; then
    echo "[*] Калькулятор (userspace, musl libc)..."
    $CC $CFLAGS_MUSL -c SysApps/calc.c -o build/calc.o
    $LD_KERN $LDFLAGS_MUSL -o build/calc.elf \
        $MUSL_OUT/lib/crt1.o $MUSL_OUT/lib/crti.o build/calc.o \
        $MUSL_OUT/lib/libc.a $MUSL_OUT/lib/crtn.o
    ls -lh build/calc.elf
elif [ -f SysApps/calc_bare.c ]; then
    echo "[!] musl не собран - собираю старый калькулятор без libc (SysApps/calc_bare.c)"
    $CC $CFLAGS_USER -c SysApps/calc_bare.c -o build/calc.o
    $LD_KERN $LDFLAGS_USER -o build/calc.elf build/calc.o
    ls -lh build/calc.elf
else
    echo "[!] musl не собран ($MUSL_OUT/lib/libc.a) - запусти ./build_musl.sh, калькулятор пропущен"
fi

rm -f build/mypc.elf
if [ -f "$MUSL_OUT/lib/libc.a" ] && [ -f SysApps/MyPC.c ]; then
    echo "[*] My Computer (userspace, musl libc)..."
    $CC $CFLAGS_MUSL -c SysApps/MyPC.c -o build/mypc.o
    $LD_KERN $LDFLAGS_MUSL -o build/mypc.elf \
        $MUSL_OUT/lib/crt1.o $MUSL_OUT/lib/crti.o build/mypc.o \
        $MUSL_OUT/lib/libc.a $MUSL_OUT/lib/crtn.o
    ls -lh build/mypc.elf
else
    echo "[!] MyPC не собран (нужны musl и SysApps/MyPC.c) - иконка My Computer на панели ничего не запустит"
fi

echo "[*] SysLogs (userspace, отдельно от ядра)..."
$CC $CFLAGS_USER -c SysApps/SysLogsApp.c -o build/syslogs.o
$LD_KERN $LDFLAGS_USER -o build/syslogs.elf build/syslogs.o
ls -lh build/syslogs.elf

echo "[*] Browser (userspace, отдельно от ядра)..."
$CC $CFLAGS_USER -c SysApps/BrowserApp.c -o build/browser.o
$LD_KERN $LDFLAGS_USER -o build/browser.elf build/browser.o
ls -lh build/browser.elf

                                                                             
                                                           
                                                                         
                                                                             
MUSL_OUT="Resources/musl/out"
CFLAGS_MUSL="-target x86_64-linux-musl -nostdinc -isystem $MUSL_OUT/include -fno-stack-protector -fno-pic -fno-pie -O2 -g -Wall -std=gnu11"
LDFLAGS_MUSL="-m elf_x86_64 -static -nostdlib -z max-page-size=4096 -z separate-code -z norelro"
MUSL_OK=0
if [ -f "$MUSL_OUT/lib/libc.a" ]; then
    echo "[*] MuslTest (userspace, musl libc)..."
    $CC $CFLAGS_MUSL -c SysApps/MuslTest.c -o build/musltest.o
    $LD_KERN $LDFLAGS_MUSL -o build/musltest.elf \
        $MUSL_OUT/lib/crt1.o $MUSL_OUT/lib/crti.o build/musltest.o \
        $MUSL_OUT/lib/libc.a $MUSL_OUT/lib/crtn.o
    ls -lh build/musltest.elf
    MUSL_OK=1
else
    echo "[!] musl не собран ($MUSL_OUT/lib/libc.a) - запусти ./build_musl.sh, MuslTest пропущен"
fi

echo "[*] Загрузчик UEFI..."
$MGW $CFLAGS_BOOT -c bootloader.c -o build/bootloader.o
$MGW $LDFLAGS_BOOT build/bootloader.o -o build/bootloader.efi

ls -lh build/kernel.bin build/bootloader.efi

TARGET_DIR="/c/fos"
mkdir -p "$TARGET_DIR/EFI/BOOT"
cp -f build/kernel.bin "$TARGET_DIR/kernel.bin"
cp -f build/bootloader.efi "$TARGET_DIR/EFI/BOOT/BOOTX64.EFI"
rm -f "$TARGET_DIR/startup.nsh" "$TARGET_DIR/OVMF.fd" "$TARGET_DIR/OVMF_VARS.fd" 2>/dev/null || true


if command -v mformat >/dev/null; then
    echo "[*] Диск build/esp.img..."
    dd if=/dev/zero of=build/esp.img bs=1M count=64 status=none
    mformat -i build/esp.img -F -v "Fexox OS" ::
    mmd -i build/esp.img ::EFI ::EFI/BOOT

    mcopy -i build/esp.img build/bootloader.efi ::EFI/BOOT/BOOTX64.EFI
    mcopy -i build/esp.img build/kernel.bin ::kernel.bin


    echo "fs0:" > build/startup.nsh
    echo "\\EFI\\BOOT\\BOOTX64.EFI" >> build/startup.nsh
    mcopy -i build/esp.img build/startup.nsh ::startup.nsh
    echo "[OK] startup.nsh успешно добавлен в образ esp.img"



    echo "[*] Диск build/data.img (virtio-blk): MBR, p1 = FAT32 (boot/seed), p2 = FHFS (тип 0xDA)..."
    # Раскладка: p1 FAT32 64 МиБ с 1 МиБ; p2 (тип 0xDA, "Fexox") 256 МиБ - ядро само форматирует её в FHFS
    # при первой загрузке. p2 при пересборке НЕ трогается (данные FHFS сохраняются).
    # Чтобы сбросить FHFS: rm build/data.img
    DATA_IMG="build/data.img"
    P1_START=2048;   P1_SECT=131072
    P2_START=133120; P2_SECT=524288
    DATA_MB=321
    DATA_FAT="$DATA_IMG@@$((P1_START * 512))"

    le32() {
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $(($1 & 255)) $((($1 >> 8) & 255)) $((($1 >> 16) & 255)) $((($1 >> 24) & 255)))"
    }

    NEED_NEW=1
    if [ -f "$DATA_IMG" ] && [ "$(stat -c%s "$DATA_IMG")" = "$((DATA_MB * 1024 * 1024))" ]; then
        NEED_NEW=0
    fi
    if [ "$NEED_NEW" = "1" ]; then
        rm -f "$DATA_IMG"
        dd if=/dev/zero of="$DATA_IMG" bs=1M count=0 seek=$DATA_MB status=none
        {
            head -c 446 /dev/zero
            printf '\x00\xfe\xff\xff\x0c\xfe\xff\xff'; le32 $P1_START; le32 $P1_SECT
            printf '\x00\xfe\xff\xff\xda\xfe\xff\xff'; le32 $P2_START; le32 $P2_SECT
            head -c 32 /dev/zero
            printf '\x55\xaa'
        } | dd of="$DATA_IMG" conv=notrunc status=none
        echo "[OK] новый $DATA_IMG (FHFS-раздел будет создан при первой загрузке)"
    fi
    mformat -i "$DATA_FAT" -F -T $P1_SECT -h 255 -s 63 -v FEXDATA ::
    echo "Fexox OS seed volume" > build/FEXOX.SEED
    mcopy -o -i "$DATA_FAT" build/FEXOX.SEED ::FEXOX.SEED


    if [ -f "init" ]; then
        echo "[*] Добавляем /init на data.img..."
        mcopy -i "$DATA_FAT" init ::init
        echo "[OK] /init добавлен"
    fi

    echo "[OK] $DATA_IMG готов"


    if [ -d "UI" ]; then
        echo "[*] Добавляем папку UI/ на data.img..."
        mmd -i "$DATA_FAT" ::UI 2>/dev/null || true
        for f in UI/*; do
            [ -f "$f" ] || continue
            mcopy -i "$DATA_FAT" "$f" "::UI/$(basename "$f")"
            echo "[OK] $f -> ::UI/$(basename "$f")"
        done
    else
        echo "[!] Папка UI/ не найдена рядом со скриптом — обои/курсор не будут скопированы"
    fi




    if [ -d "APPS" ]; then
        echo "[*] Добавляем папку APPS/ на data.img..."
        mmd -i "$DATA_FAT" ::APPS 2>/dev/null || true
        for f in APPS/*; do
            [ -f "$f" ] || continue
            mcopy -i "$DATA_FAT" "$f" "::APPS/$(basename "$f")"
            echo "[OK] $f -> ::APPS/$(basename "$f")"
        done
    else
        echo "[!] Папка APPS/ не найдена — .fxapp манифесты не будут скопированы (не критично)"
    fi




    mmd -i "$DATA_FAT" ::APPS 2>/dev/null || true
    if [ -f build/calc.elf ]; then
        mcopy -o -i "$DATA_FAT" build/calc.elf ::APPS/calc.elf
        echo "[OK] build/calc.elf -> ::APPS/calc.elf"
    fi

    if [ -f build/mypc.elf ]; then
        mcopy -o -i "$DATA_FAT" build/mypc.elf ::APPS/mypc.elf
        echo "[OK] build/mypc.elf -> ::APPS/mypc.elf"
    fi

    mcopy -i "$DATA_FAT" build/syslogs.elf ::APPS/syslogs.elf
    echo "[OK] build/syslogs.elf -> ::APPS/syslogs.elf"

    mcopy -i "$DATA_FAT" build/browser.elf ::APPS/browser.elf
    echo "[OK] build/browser.elf -> ::APPS/browser.elf"

    if [ "$MUSL_OK" = "1" ]; then
        mcopy -i "$DATA_FAT" build/musltest.elf ::APPS/musltest.elf
        echo "[OK] build/musltest.elf -> ::APPS/musltest.elf"
    fi
else
    echo "[!] mtools нет — pacman -S mingw-w64-ucrt-x86_64-mtools"
fi

echo "[+] build/kernel.bin  build/bootloader.efi  build/esp.img  build/data.img  build/calc.elf  build/syslogs.elf  build/browser.elf"
# Образ для реальной флешки (MBR: FAT32 + раздел FHFS). Отключить: BUILD_USB_IMG=0 ./Compile.sh
if [ "${BUILD_USB_IMG:-1}" = "1" ] && [ -f make_usb_image.sh ] && command -v mformat >/dev/null; then
    echo "[*] build/usb.img (для флешки)..."
    QUIET=1 bash ./make_usb_image.sh | tail -n 3
fi

# Образ BIOS+UEFI (MBR-загрузчик stage1/stage2 + FAT32 + FHFS). Отключить: BUILD_BIOS_IMG=0 ./Compile.sh
if [ "${BUILD_BIOS_IMG:-1}" = "1" ] && [ -f build_bios.sh ] && [ -f make_bios_image.sh ] && [ -f BIOS/stage1.S ] && command -v mformat >/dev/null; then
    echo "[*] build/usb_bios.img (BIOS + UEFI)..."
    if bash ./build_bios.sh >/dev/null && QUIET=1 bash ./make_bios_image.sh | tail -n 3; then :; else
        echo "[!] usb_bios.img не собран (см. build_bios.sh) - не критично"
    fi
fi

echo "=== Готово ==="
