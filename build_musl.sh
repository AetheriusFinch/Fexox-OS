#!/bin/bash
# Fexox OS Project, EULA GPLv2
                                                        
                                                                    
                                                                                       
set -e
cd "$(dirname "$0")"
MUSL_DIR="Resources/musl"

[ -f "$MUSL_DIR/configure" ] || { echo "[!] $MUSL_DIR/configure не найден: положи исходники musl в $MUSL_DIR"; exit 1; }
for t in clang llvm-ar llvm-ranlib make sed; do
    command -v "$t" >/dev/null || { echo "[!] нет '$t'. Установи: pacman -S --needed mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-llvm make sed"; exit 1; }
done

cd "$MUSL_DIR"
OUT="$PWD/out"

if [ -f Makefile ]; then make distclean >/dev/null 2>&1 || true; fi
rm -rf "$OUT"

export CC="clang --target=x86_64-linux-musl"
export AR=llvm-ar
export RANLIB=llvm-ranlib
CFLAGS="-O2 -fno-stack-protector -fno-pic"

echo "[*] configure musl (x86_64, static)..."
./configure --target=x86_64 --prefix="$OUT" --disable-shared CFLAGS="$CFLAGS"

echo "[*] make musl..."
JOBS="$(nproc 2>/dev/null || echo 4)"
                                                                              
                                                                                   
                                                                              
make -j"$JOBS" || true
if [ ! -s lib/libc.a ]; then
    echo "[*] собираю lib/libc.a порциями (обход лимита длины командной строки)..."
    make -n lib/libc.a 2>/dev/null \
        | grep -E '^[[:space:]]*llvm-ar[[:space:]]+rc[[:space:]]+lib/libc\.a' | tail -n1 \
        | sed -E 's/^[[:space:]]*llvm-ar[[:space:]]+rc[[:space:]]+lib\/libc\.a[[:space:]]+//' \
        | tr ' ' '\n' | grep -v '^$' > /tmp/libc_objs.txt
    [ -s /tmp/libc_objs.txt ] || { echo "[!] не удалось получить список объектов (make -n)"; exit 1; }
    rm -f lib/libc.a
    xargs -n 100 llvm-ar qc lib/libc.a < /tmp/libc_objs.txt
    llvm-ranlib lib/libc.a
    echo "[OK] libc.a: $(wc -l < /tmp/libc_objs.txt) объектов"
fi
make -j"$JOBS"

echo "[*] install -> $OUT"
make install

for f in lib/libc.a lib/crt1.o lib/crti.o lib/crtn.o include/stdio.h; do
    [ -f "$OUT/$f" ] || { echo "[!] после сборки нет $OUT/$f"; exit 1; }
done
echo "[OK] musl готов: $OUT"
