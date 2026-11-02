#!/bin/sh
# Builds apparition.html from the source template: inlines the 3D amp module and the DI loop (base64 WAV).
cd "$(dirname "$0")/.."
python3 - "$1" <<'PY'
import sys
b64=open(sys.argv[1]).read().strip()
s=open('src/apparition.src.html').read().replace('/*__AMP3D__*/',open('src/amp3d.js').read()).replace('__DI_B64__',b64)
open('apparition.html','w').write(s)
PY
