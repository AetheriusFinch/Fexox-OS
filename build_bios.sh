#!/bin/bash
# Fexox OS Project, EULA GPLv2





set -e
echo "=== Fexox OS: сборка BIOS-загрузчика ==="

CC="${CC:-clang}"
LD="${LD:-ld.lld}"
command -v "$CC" >/dev/null || { echo "[!] $CC не найден"; exit 1; }
command -v "$LD" >/dev/null || { echo "[!] $LD не найден"; exit 1; }

OUT=build/bios
mkdir -p "$OUT"


CFLAGS32="-target i386-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
-mno-sse -mno-mmx -Os -Wall -Wextra -fno-asynchronous-unwind-tables -fno-delete-null-pointer-checks -nostdlib"



link_bin() {
    local script="$1" out="$2"; shift 2
    if $LD -m elf_i386 -T "$script" --oformat binary -o "$out" "$@" 2>/dev/null; then
        return 0
    fi
    local elf="${out%.bin}.elf"
    $LD -m elf_i386 -T "$script" -o "$elf" "$@"
    local OBJCOPY
    OBJCOPY="$(command -v llvm-objcopy || command -v objcopy || true)"
    [ -n "$OBJCOPY" ] || { echo "[!] нет ни llvm-objcopy, ни objcopy"; exit 1; }
    "$OBJCOPY" -O binary "$elf" "$out"
}

echo "[*] stage1 (MBR)..."
$CC -target i386-none-elf -c BIOS/stage1.S -o "$OUT/stage1.o"
link_bin BIOS/stage1.ld "$OUT/stage1.bin" "$OUT/stage1.o"
S1=$(wc -c < "$OUT/stage1.bin")
[ "$S1" -le 446 ] || { echo "[!] stage1.bin = $S1 байт (> 446, не влезет в MBR)"; exit 1; }
echo "[OK] stage1.bin: $S1 байт (лимит 446)"

echo "[*] stage2..."
$CC -target i386-none-elf -c BIOS/stage2_entry.S -o "$OUT/stage2_entry.o"
$CC $CFLAGS32 -c BIOS/stage2.c -o "$OUT/stage2.o"

link_bin BIOS/stage2.ld "$OUT/stage2.bin" "$OUT/stage2_entry.o" "$OUT/stage2.o"
S2=$(wc -c < "$OUT/stage2.bin")
[ "$S2" -le 40960 ] || { echo "[!] stage2.bin = $S2 байт (> 40960, stage1 читает 80 секторов)"; exit 1; }
echo "[OK] stage2.bin: $S2 байт (лимит 40960)"

echo "=== Готово: $OUT/stage1.bin  $OUT/stage2.bin ==="
