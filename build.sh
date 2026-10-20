#!/usr/bin/env bash
# Baut libvulkan_exynos_example.so und packt ein AdrenoTools-kompatibles ZIP.
# Nutzung: ./build.sh /pfad/zum/android-ndk     (oder ANDROID_NDK_HOME setzen)
set -euo pipefail
NDK="${1:-${ANDROID_NDK_HOME:-}}"
[ -d "$NDK" ] || { echo "FEHLER: NDK-Pfad fehlt. Aufruf: ./build.sh /pfad/zum/ndk" >&2; exit 1; }
HOST=$(ls "$NDK/toolchains/llvm/prebuilt")
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android34-clang"
OUT=out; LIB=libvulkan_exynos_example.so
rm -rf "$OUT"; mkdir -p "$OUT"
"$CC" -shared -fPIC -O2 -Wall -fvisibility=hidden -o "$OUT/$LIB" exynos_example.c -llog -ldl
cat > "$OUT/meta.json" <<JSON
{
  "schemaVersion": 1,
  "name": "ExynosExample",
  "description": "Beispiel-Passthrough-Layer fuer Xclipse (nur Demo)",
  "author": "Beispiel",
  "packageVersion": "1",
  "vendor": "Samsung",
  "driverVersion": "0.1-example",
  "minApi": 28,
  "libraryName": "$LIB"
}
JSON
(cd "$OUT" && zip -q ../ExynosExample-0.1.zip "$LIB" meta.json)
echo "Fertig: $(pwd)/ExynosExample-0.1.zip"
