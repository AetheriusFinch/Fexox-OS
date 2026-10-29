#!/bin/bash
# Fexox OS Project, EULA GPLv2
set -e
echo "=== Fexox OS Build Script (MSYS2 UCRT64) ==="

CC="clang"
LD_KERN="ld.lld"

command -v $CC >/dev/null || { echo "[!] clang не найден"; exit 1; }

mkdir -p build

CFLAGS_KERN="-I. -IResources -target x86_64-unknown-none-elf -ffreestanding -fno-builtin -mno-red-zone -mcmodel=kernel -fno-pie -fno-stack-protector -mgeneral-regs-only -O0 -g -Wall -Wextra -std=c11"
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
$CC $CFLAGS_KERN -c IDE.c              -o build/IDE.o
$CC $CFLAGS_KERN -c AHCI.c             -o build/AHCI.o
$CC $CFLAGS_KERN -c NVMe.c             -o build/NVMe.o
$CC $CFLAGS_KERN -c klibc.c            -o build/klibc.o
$CC $CFLAGS_KERN -c ELF64.c            -o build/ELF64.o
$CC $CFLAGS_KERN -c Framebuffer.c      -o build/Framebuffer.o
$CC $CFLAGS_KERN -c png.c              -o build/png.o
$CC $CFLAGS_KERN -c ui_extra.c         -o build/ui_extra.o
$CC $CFLAGS_KERN -c PS2Mouse.c         -o build/PS2Mouse.o
$CC $CFLAGS_KERN -c AcpiControl.c      -o build/AcpiControl.o
$CC $CFLAGS_KERN -c Resources/fxapp.c  -o build/fxapp.o
$CC $CFLAGS_KERN -c Resources/USB.c    -o build/USB.o
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
    build/IDE.o            \
    build/AHCI.o           \
    build/NVMe.o           \
    build/klibc.o          \
    build/ELF64.o          \
    build/Framebuffer.o    \
    build/png.o            \
    build/ui_extra.o        \
    build/PS2Mouse.o        \
	build/AcpiControl.o     \
	build/fxapp.o           \
    build/USB.o             \
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

echo "[*] Калькулятор (userspace, отдельно от ядра)..."
$CC $CFLAGS_USER -c SysApps/calc.c -o build/calc.o
$LD_KERN $LDFLAGS_USER -o build/calc.elf build/calc.o
ls -lh build/calc.elf

echo "[*] SysLogs (userspace, отдельно от ядра)..."
$CC $CFLAGS_USER -c SysApps/SysLogsApp.c -o build/syslogs.o
$LD_KERN $LDFLAGS_USER -o build/syslogs.elf build/syslogs.o
ls -lh build/syslogs.elf

echo "[*] Browser (userspace, отдельно от ядра)..."
$CC $CFLAGS_USER -c SysApps/BrowserApp.c -o build/browser.o
$LD_KERN $LDFLAGS_USER -o build/browser.elf build/browser.o
ls -lh build/browser.elf

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



    echo "[*] Диск build/data.img (virtio-blk data disk)..."
    dd if=/dev/zero of=build/data.img bs=1M count=64 status=none
    mformat -i build/data.img -F -v FEXDATA ::


    if [ -f "init" ]; then
        echo "[*] Добавляем /init на data.img..."
        mcopy -i build/data.img init ::init
        echo "[OK] /init добавлен"
    fi

    echo "[OK] build/data.img готов"


    if [ -d "UI" ]; then
        echo "[*] Добавляем папку UI/ на data.img..."
        mmd -i build/data.img ::UI 2>/dev/null || true
        for f in UI/*; do
            [ -f "$f" ] || continue
            mcopy -i build/data.img "$f" "::UI/$(basename "$f")"
            echo "[OK] $f -> ::UI/$(basename "$f")"
        done
    else
        echo "[!] Папка UI/ не найдена рядом со скриптом — обои/курсор не будут скопированы"
    fi




    if [ -d "APPS" ]; then
        echo "[*] Добавляем папку APPS/ на data.img..."
        mmd -i build/data.img ::APPS 2>/dev/null || true
        for f in APPS/*; do
            [ -f "$f" ] || continue
            mcopy -i build/data.img "$f" "::APPS/$(basename "$f")"
            echo "[OK] $f -> ::APPS/$(basename "$f")"
        done
    else
        echo "[!] Папка APPS/ не найдена — .fxapp манифесты не будут скопированы (не критично)"
    fi




    mmd -i build/data.img ::APPS 2>/dev/null || true
    mcopy -i build/data.img build/calc.elf ::APPS/calc.elf
    echo "[OK] build/calc.elf -> ::APPS/calc.elf"

    mcopy -i build/data.img build/syslogs.elf ::APPS/syslogs.elf
    echo "[OK] build/syslogs.elf -> ::APPS/syslogs.elf"

    mcopy -i build/data.img build/browser.elf ::APPS/browser.elf
    echo "[OK] build/browser.elf -> ::APPS/browser.elf"
else
    echo "[!] mtools нет — pacman -S mingw-w64-ucrt-x86_64-mtools"
fi

echo "[+] build/kernel.bin  build/bootloader.efi  build/esp.img  build/data.img  build/calc.elf  build/syslogs.elf  build/browser.elf"
echo "=== Готово ==="
