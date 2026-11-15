#!/usr/bin/env bash
set -euo pipefail

AIT_VERSION="1.9.1"
AIT_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
AIT_URL="https://github.com/AppImage/appimagetool/releases/download/$AIT_VERSION/appimagetool-x86_64.AppImage"

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS="${BSL_DEPS_PREFIX:-$HOME/bsl-deps}"
BUILD="${BSL_APPIMAGE_BUILD:-$RAIZ/build-appimage}"
DIST="${BSL_APPIMAGE_DIST:-$RAIZ/dist}"
FERRAMENTAS="${BSL_APPIMAGE_TOOLS:-$HOME/.cache/bsl-stage-tools}"
CMAKE="${CMAKE:-cmake}"
JOBS="${BSL_APPIMAGE_JOBS:-8}"

export PATH="$(echo "$PATH" | tr ':' '\n' | grep -v '^/mnt/' | paste -sd:)"
export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig"
export CC="${CC:-gcc-12}"
export CXX="${CXX:-g++-12}"

[ "$(uname -m)" = "x86_64" ] || { echo "Este script gera AppImage x86_64."; exit 1; }
[ -f "$DEPS/lib/pkgconfig/libavcodec.pc" ] || {
    echo "FFmpeg proprio nao encontrado em $DEPS. Rode antes: tools/package/build-ffmpeg-linux.sh"
    exit 1
}
for f in "$CMAKE" ninja "$CC" "$CXX" curl sha256sum readelf ldd strip; do
    command -v "$f" >/dev/null 2>&1 || { echo "Falta o programa: $f"; exit 1; }
done

VERSAO="$(sed -n 's/^project(BSLStage VERSION \([0-9][0-9.]*\).*/\1/p' "$RAIZ/CMakeLists.txt")"
[ -n "$VERSAO" ] || { echo "Nao achei a versao em CMakeLists.txt"; exit 1; }

echo "Compilando BSL Stage $VERSAO..."
"$CMAKE" -S "$RAIZ" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH='$ORIGIN;$ORIGIN/../lib' \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" >/dev/null
"$CMAKE" --build "$BUILD" --target BSLStage bslstage_worker -j "$JOBS"

APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR"
BIN="$APPDIR/usr/bin"
LIB="$APPDIR/usr/lib"
DOC="$APPDIR/usr/share/doc/bsl-stage"
mkdir -p "$BIN" "$LIB" "$DOC" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

install -m 755 "$BUILD/BSLStage" "$BIN/BSLStage"
install -m 755 "$BUILD/bslstage_worker" "$BIN/bslstage_worker"
strip --strip-unneeded "$BIN/BSLStage" "$BIN/bslstage_worker"

for f in "$BUILD"/libonnxruntime.so "$BUILD"/libpdfium.so; do
    [ -f "$f" ] || { echo "Faltou $(basename "$f") em $BUILD"; exit 1; }
    install -m 755 "$f" "$BIN/"
done

cp -a "$DEPS"/lib/libavformat.so* "$DEPS"/lib/libavcodec.so* "$DEPS"/lib/libavutil.so* \
      "$DEPS"/lib/libswscale.so* "$DEPS"/lib/libswresample.so* "$DEPS"/lib/libavdevice.so* "$LIB/"
for f in "$LIB"/*.so.*.*; do
    [ -L "$f" ] || strip --strip-unneeded "$f"
done

cp -r "$RAIZ/assets" "$BIN/assets"
cp -r "$RAIZ/licenses" "$BIN/licenses"
for f in LICENSE THIRD-PARTY-NOTICES.md PRIVACIDADE.md TERMOS-DE-USO.md; do
    cp "$RAIZ/$f" "$BIN/$f"
    cp "$RAIZ/$f" "$DOC/$f"
done
cp -r "$RAIZ/licenses" "$DOC/licenses"
for f in COPYING.LGPLv2.1 COPYING.LGPLv3 LICENSE.md libvpx-PATENTS.txt; do
    cp "$DEPS/share/doc/ffmpeg/$f" "$DOC/$f"
done
cp "$DEPS/share/doc/ffmpeg/CONFIGURE-FFMPEG.txt" "$DOC/FFMPEG-CONFIGURE.txt"
cp "$DEPS/share/doc/ffmpeg/CONFIGURE-FFMPEG.txt" "$BIN/licenses/FFMPEG-CONFIGURE.txt"

ICONE="$RAIZ/assets/brand/bsl-mark.png"
cp "$ICONE" "$APPDIR/bsl-stage.png"
cp "$ICONE" "$APPDIR/usr/share/icons/hicolor/256x256/apps/bsl-stage.png"
cp "$ICONE" "$APPDIR/.DirIcon"

cat > "$APPDIR/usr/share/applications/bsl-stage.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=BSL Stage
Comment=Software de VJ e mapeamento de video
Comment[pt_BR]=Software de VJ e mapeamento de video
Exec=BSLStage %F
Icon=bsl-stage
Terminal=false
Categories=AudioVideo;Video;Graphics;
StartupWMClass=BSLStage
DESKTOP
cp "$APPDIR/usr/share/applications/bsl-stage.desktop" "$APPDIR/bsl-stage.desktop"

cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
AQUI="$(dirname "$(readlink -f "$0")")"
exec "$AQUI/usr/bin/BSLStage" "$@"
APPRUN
chmod 755 "$APPDIR/AppRun"

echo "Conferindo dependencias..."
BASE='^(linux-vdso|ld-linux|libc|libm|libdl|libpthread|librt|libutil|libresolv|libstdc\+\+|libgcc_s|libz|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libX11|libXext|libXrandr|libXrender|libXi|libXcursor|libXinerama|libXfixes|libXxf86vm|libXau|libXdmcp|libxcb|libwayland|libxkbcommon|libpulse|libdbus-1|libasound|libdrm|libsystemd|libudev|liblzma|liblz4|libgcrypt|libgpg-error|libcap|libzstd)[-.0-9a-zA-Z_+]*$'
for f in "$BIN/BSLStage" "$BIN/bslstage_worker" "$BIN/libonnxruntime.so" "$BIN/libpdfium.so" "$LIB"/*.so.*.*; do
    [ -L "$f" ] && continue
    ldd "$f" 2>&1 | while read -r nome seta caminho resto; do
        [ -z "$nome" ] && continue
        case "$caminho" in
            "$APPDIR"/*) continue ;;
        esac
        if basename "$nome" | grep -Eq "$BASE"; then continue; fi
        echo "Dependencia fora do AppDir: $(basename "$f") -> $nome $seta $caminho"
    done
done > "$BUILD/ldd-check.txt"
if [ -s "$BUILD/ldd-check.txt" ]; then
    cat "$BUILD/ldd-check.txt"
    echo "Ha dependencias fora do AppDir e fora da lista de bibliotecas base."
    exit 1
fi

mkdir -p "$FERRAMENTAS" "$DIST"
AIT="$FERRAMENTAS/appimagetool-$AIT_VERSION-x86_64.AppImage"
if [ ! -f "$AIT" ]; then
    curl -fsSL -o "$AIT" "$AIT_URL"
fi
echo "$AIT_SHA256  $AIT" | sha256sum -c - >/dev/null || { rm -f "$AIT"; echo "SHA-256 invalido do appimagetool."; exit 1; }
chmod +x "$AIT"

SAIDA="$DIST/BSL-Stage-$VERSAO-x86_64.AppImage"
rm -f "$SAIDA"
ARCH=x86_64 "$AIT" --appimage-extract-and-run --comp zstd "$APPDIR" "$SAIDA"
chmod +x "$SAIDA"
echo "AppImage pronto: $SAIDA"
ls -l "$SAIDA"
