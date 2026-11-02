#!/bin/sh
# Builds an update for the in-plugin update button: pack zip + manifest.json in build/release/<version>/.
# usage: tools/release.sh <version> "<release notes>" [baseUrl]
#   baseUrl = where the zip will be downloadable (default: GitHub release download URL for that tag)
#   SRC_DIR defaults to ../src (the web source); set PUBLISH=1 to upload as a GitHub release with gh.
set -e
cd "$(dirname "$0")/.."
VER="$1"; NOTES="$2"
[ -n "$VER" ] || { echo "usage: tools/release.sh <version> \"notes\" [baseUrl]"; exit 1; }
REPO="${GL_REPO:-timetellervideography/ghostlike-apparition}"
BASE="${3:-https://github.com/$REPO/releases/download/v$VER}"
SRC_DIR="${SRC_DIR:-../src}"
OUT="build/release/$VER"; STAGE="build/release/stage-$VER"
rm -rf "$OUT" "$STAGE"; mkdir -p "$OUT"

node tools/make_pack.mjs "$SRC_DIR" "$STAGE" "$VER"
cmake -S . -B build -DGL_PACK_VERSION="$VER" >/dev/null
cmake --build build --target GhostlikeEngine -j8 >/dev/null
cp build/libGhostlikeEngine.dylib "$STAGE/"
codesign --force -s "${SIGN_ID:--}" "$STAGE/libGhostlikeEngine.dylib"
ZIP="apparition-pack-$VER.zip"
(cd "$STAGE" && zip -qr -X "../$VER/$ZIP" .)
SHA=$(shasum -a 256 "$OUT/$ZIP" | cut -d' ' -f1); SIZE=$(stat -f%z "$OUT/$ZIP")
node -e '
const [ver,notes,url,sha,size,out]=process.argv.slice(1);
require("fs").writeFileSync(out,JSON.stringify({version:ver,abi:1,minShell:"1.0.0",notes,pack:{url,sha256:sha,size:+size},
  shell:{version:"1.0.0",url:"'"https://github.com/$REPO/releases/latest"'"}},null,1));' \
  "$VER" "$NOTES" "$BASE/$ZIP" "$SHA" "$SIZE" "$OUT/manifest.json"
rm -rf "$STAGE"
echo "release $VER: $OUT/$ZIP ($SIZE bytes, sha256 $SHA)"
if [ "$PUBLISH" = 1 ]; then
  gh release create "v$VER" "$OUT/$ZIP" "$OUT/manifest.json" --repo "$REPO" --title "Apparition $VER" --notes "$NOTES"
fi
