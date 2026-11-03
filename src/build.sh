#!/bin/sh
# Builds apparition.html (web preview) from the source template: inlines the 3D modules and the DI loop (base64 WAV).
# usage: src/build.sh [di.wav]   (default: src/ghostlike-di.wav, kept out of git)
cd "$(dirname "$0")/.."
python3 - "${1:-src/ghostlike-di.wav}" <<'PY'
import sys, base64
b64=base64.b64encode(open(sys.argv[1],'rb').read()).decode()
js=open('src/amp3d.js').read()+'\n'+open('src/pedal3d.js').read()
s=open('src/apparition.src.html').read().replace('/*__AMP3D__*/',js).replace('__DI_B64__',b64)
open('apparition.html','w').write(s)
PY
