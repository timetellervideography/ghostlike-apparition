# Ghostlike Apparition – Plugin

AU/VST3-Plugin (macOS, Apple Silicon + Intel). Klang, Amp-Modelle, Presets und die komplette Optik kommen aus einem
**Content-Pack** und werden über den Update-Knopf im Plugin ausgetauscht – ohne Neuinstallation.

| Teil | Wo | Wie aktualisiert |
|---|---|---|
| Shell (Logic-Anbindung, Fenster, Update-Knopf) | `shell/` | einmalig per Installer (`tools/make_installer.sh`) |
| DSP-Engine (`libGhostlikeEngine.dylib`) | `engine/` | Update-Knopf (wird live getauscht) |
| Amp-/Cab-Daten, Presets, Oberfläche | `../src/` (Web-Quelle) | Update-Knopf |

Die Web-Vorschau (`../src/apparition.src.html`) ist die einzige Quelle für Werte und Oberfläche; `tools/make_pack.mjs`
baut daraus das Pack. `tools/render` prüft, dass die C++-Engine wie die Vorschau klingt (±0,1 dB).

## Befehle
```sh
tools/build.sh                 # Pack + Plugin bauen, Pack in die Bundles, signieren
tools/make_installer.sh        # build/installer/Ghostlike-Apparition-<ver>.pkg
tools/release.sh 1.0.1 "Notizen"            # Update-Zip + manifest.json in build/release/1.0.1
PUBLISH=1 tools/release.sh 1.0.1 "Notizen"  # … und als GitHub-Release hochladen
```
Update-Adresse: `GL_UPDATE_URL` (CMake) bzw. `updateUrl` in `pack.json`; zum Testen
`~/Library/Application Support/Ghostlike/Apparition/update-url.txt`.

Engine-ABI ändern (`engine/gl_engine_api.h`) erfordert einen neuen Installer – alles andere nicht.
