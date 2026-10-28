#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -n "${PSPDEV:-}" ]]; then export PATH="$PSPDEV/bin:$PATH"; fi
command -v psp-config >/dev/null || { echo 'Нужен PSPDEV в PATH или переменной PSPDEV.' >&2; exit 1; }
if [[ -n "${PSPBUILD:-}" ]]; then signer="$PSPBUILD"
elif command -v pspbuild >/dev/null 2>&1; then signer="$(command -v pspbuild)"
else echo 'Нужен pspbuild; задайте путь в PSPBUILD.' >&2; exit 1; fi
make -j2
mkdir -p dist/PSP/GAME/DALREYS
TEMP_ENCRYPTED="$(mktemp -t dalreys-enc.XXXXXXXX)"
TEMP_RECOVERED="$(mktemp -t dalreys-prx.XXXXXXXX)"
trap 'rm -f "$TEMP_ENCRYPTED" "$TEMP_RECOVERED"' EXIT
"$signer" encrypt EBOOT.PBP -o "$TEMP_ENCRYPTED"
python3 scripts/compat_pbp.py "$TEMP_ENCRYPTED" assets/compat/PARAM.SFO ICON0.PNG dist/PSP/GAME/DALREYS/EBOOT.PBP
"$signer" verify dist/PSP/GAME/DALREYS/EBOOT.PBP
"$signer" decrypt dist/PSP/GAME/DALREYS/EBOOT.PBP -o "$TEMP_RECOVERED"
cmp "$TEMP_RECOVERED" RoadHaul.prx
python3 scripts/check_release.py dist/PSP/GAME/DALREYS/EBOOT.PBP
python3 - <<'ZIP'
from pathlib import Path
import zipfile
p=Path('dist/PSP/GAME/DALREYS/EBOOT.PBP')
with zipfile.ZipFile('DALNIY_REYS_PSP_1.4_TRAFFIC.zip','w',zipfile.ZIP_DEFLATED) as z:
 z.write(p,'PSP/GAME/DALREYS/EBOOT.PBP')
ZIP
