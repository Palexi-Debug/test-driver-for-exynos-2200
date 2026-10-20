#!/usr/bin/env bash
# Baut libvulkan_exynos_gta.so und packt ein AdrenoTools-kompatibles ZIP.
# Nutzung: ./build.sh /pfad/zum/android-ndk     (oder ANDROID_NDK_HOME setzen)
set -euo pipefail
NDK="${1:-${ANDROID_NDK_HOME:-}}"
[ -d "$NDK" ] || { echo "FEHLER: NDK-Pfad fehlt. Aufruf: ./build.sh /pfad/zum/ndk" >&2; exit 1; }
HOST=$(ls "$NDK/toolchains/llvm/prebuilt")
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android34-clang"
OUT=out; LIB=libvulkan_exynos_gta.so
rm -rf "$OUT"; mkdir -p "$OUT"
"$CC" -shared -fPIC -O2 -Wall -fvisibility=hidden -o "$OUT/$LIB" exynos_example.c -llog -ldl
cat > "$OUT/meta.json" <<JSON
{
  "schemaVersion": 1,
  "name": "ExynosGTA",
  "description": "GTA-V-Profil: Passthrough + FPS/Frametime-Messung (Xclipse)",
  "author": "Beispiel",
  "packageVersion": "1",
  "vendor": "Samsung",
  "driverVersion": "0.2-gta",
  "minApi": 28,
  "libraryName": "$LIB"
}
JSON
(cd "$OUT" && zip -q ../ExynosGTA-0.2.zip "$LIB" meta.json)
echo "Fertig: $(pwd)/ExynosGTA-0.2.zip"
