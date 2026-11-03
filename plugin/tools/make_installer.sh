#!/bin/sh
# Builds the one-time installer: AU + VST3 into /Library/Audio/Plug-Ins (factory pack inside the bundles).
# Run tools/build.sh first. Set SIGN_INSTALLER_ID to a "Developer ID Installer" identity to sign the pkg.
set -e
cd "$(dirname "$0")/.."
VER=$(sed -n 's/^project(GhostlikeApparition VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)   # shell version (package receipts)
PACK=$(node -p 'require("./build/pack/pack.json").version')                          # what the user sees
ART=build/GhostlikeApparition_artefacts/Release; OUT=build/installer; W="$OUT/work"
rm -rf "$OUT"; mkdir -p "$W/au/root" "$W/vst3/root" "$W/scripts"
cp -R "$ART/AU/Ghostlike Apparition.component" "$W/au/root/"
cp -R "$ART/VST3/Ghostlike Apparition.vst3" "$W/vst3/root/"
cat > "$W/scripts/postinstall" <<'SH'
#!/bin/sh
# let Logic see the new Audio Unit without a reboot
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
SH
chmod +x "$W/scripts/postinstall"
for k in au vst3; do
  pkgbuild --analyze --root "$W/$k/root" "$W/$k.plist" >/dev/null
  plutil -replace 0.BundleIsRelocatable -bool NO "$W/$k.plist"   # always install to the plug-in folder
done
pkgbuild --root "$W/au/root" --component-plist "$W/au.plist" --install-location "/Library/Audio/Plug-Ins/Components" \
  --identifier de.ghostlike.apparition.au --version "$VER" --scripts "$W/scripts" "$W/au.pkg" >/dev/null
pkgbuild --root "$W/vst3/root" --component-plist "$W/vst3.plist" --install-location "/Library/Audio/Plug-Ins/VST3" \
  --identifier de.ghostlike.apparition.vst3 --version "$VER" "$W/vst3.pkg" >/dev/null
mkdir -p "$W/res"
cat > "$W/res/welcome.html" <<'HTML'
<html><body style="font-family:-apple-system,sans-serif"><h2>Ghostlike Apparition</h2>
<p>Röhren-Amp-Plugin für Ghostlike. Installiert das Audio Unit (Logic, GarageBand) und VST3.</p>
<p>Nach der Installation Logic neu starten. Spätere Sounds, Amps und Optik-Updates kommen über <b>Einstellungen → Nach Updates suchen</b> im Plugin – ohne neuen Installer.</p></body></html>
HTML
cat > "$W/dist.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Ghostlike Apparition $PACK</title>
  <welcome file="welcome.html"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline><line choice="au"/><line choice="vst3"/></choices-outline>
  <choice id="au" title="Audio Unit (Logic, GarageBand)"><pkg-ref id="de.ghostlike.apparition.au"/></choice>
  <choice id="vst3" title="VST3 (Ableton, Cubase, Reaper …)"><pkg-ref id="de.ghostlike.apparition.vst3"/></choice>
  <pkg-ref id="de.ghostlike.apparition.au">au.pkg</pkg-ref>
  <pkg-ref id="de.ghostlike.apparition.vst3">vst3.pkg</pkg-ref>
</installer-gui-script>
XML
PKG="$OUT/Ghostlike-Apparition-$PACK.pkg"
if [ -n "$SIGN_INSTALLER_ID" ]; then SIGN="--sign $SIGN_INSTALLER_ID"; else SIGN=""; fi
productbuild --distribution "$W/dist.xml" --package-path "$W" --resources "$W/res" $SIGN "$PKG" >/dev/null
rm -rf "$W"
echo "installer: $PKG ($(du -h "$PKG" | cut -f1))"
