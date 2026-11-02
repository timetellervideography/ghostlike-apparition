#!/bin/sh
# Full build: content pack → engine + plugin shell → factory pack into every bundle → code signing.
# usage: tools/build.sh [packVersion]   (SRC_DIR defaults to ../src, the web source)
set -e
cd "$(dirname "$0")/.."
VER="${1:-$(sed -n 's/.*GL_PACK_VERSION "\([^"]*\)".*/\1/p' CMakeLists.txt)}"
SRC_DIR="${SRC_DIR:-../src}"
[ -d third_party/JUCE ] || git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git third_party/JUCE
node tools/make_pack.mjs "$SRC_DIR" build/pack "$VER"
[ -f build/CMakeCache.txt ] || cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake -S . -B build -DGL_PACK_VERSION="$VER" >/dev/null
cmake --build build -j8
ART=build/GhostlikeApparition_artefacts/Release
ID="${SIGN_ID:--}"   # "-" = ad-hoc; set SIGN_ID to a Developer ID for distribution
for B in "$ART/AU/Ghostlike Apparition.component" "$ART/VST3/Ghostlike Apparition.vst3" "$ART/Standalone/Ghostlike Apparition.app"; do
  F="$B/Contents/Resources/factory"
  rm -rf "$F" && mkdir -p "$F" && cp -R build/pack/ "$F/" && cp build/libGhostlikeEngine.dylib "$F/"
  codesign --force -s "$ID" "$F/libGhostlikeEngine.dylib"
  codesign --force --deep -s "$ID" "$B"
done
echo "built pack $VER into AU, VST3, Standalone"
