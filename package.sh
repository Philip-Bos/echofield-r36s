#!/bin/bash
# Build and package ECHOFIELD as a PortMaster port for the R36S.
# Output: dist/ECHOFIELD_R36S_PortMaster.zip  (extract into /roms/ports/)
set -euo pipefail
cd "$(dirname "$0")"
VERSION=$(grep -o 'EF_VERSION "[^"]*"' src/common.h | cut -d'"' -f2)

if [ "${1:-}" != "--no-build" ]; then
  ./build.sh r36s
  ./build.sh host
  # fresh screenshot for the PortMaster metadata (rendered by the real UI, headless)
  rm -rf build/shots build/shotdata && mkdir -p build/shots build/shotdata
  ( cd port/echofield && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ECHOFIELD_APPDIR="$PWD" \
      ../../build/echofield.host --windowed --data ../../build/shotdata --screenshots ../../build/shots >/dev/null 2>&1 || true )
  python3 -c "
from PIL import Image
im = Image.open('build/shots/00_main.bmp').convert('RGB')
im.save('port/echofield/screenshot.png'); im.save('port/echofield/cover.png')"
fi

STAGE=build/stage
rm -rf "$STAGE" && mkdir -p "$STAGE/echofield"
cp port/Echofield.sh "$STAGE/"
cp build/echofield.aarch64 "$STAGE/echofield/"
cp -r port/echofield/assets port/echofield/licenses "$STAGE/echofield/"
cp port/echofield/port.json port/echofield/gameinfo.xml port/echofield/screenshot.png port/echofield/cover.png "$STAGE/echofield/"
cp README.md "$STAGE/echofield/README.md"
cp LICENSE "$STAGE/echofield/licenses/ECHOFIELD_LICENSE.txt"
cp docs/HARDWARE_TESTS.md "$STAGE/echofield/HARDWARE_TESTS.md"
chmod 755 "$STAGE/Echofield.sh" "$STAGE/echofield/echofield.aarch64"
# never ship user data: updates must not overwrite recordings/presets/projects
test ! -e "$STAGE/echofield/userdata"

mkdir -p dist
OUT=dist/ECHOFIELD_R36S_PortMaster.zip
rm -f "$OUT"
python3 - "$STAGE" "$OUT" <<'EOF'
import os, sys, zipfile, stat
stage, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(stage):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            arc = os.path.relpath(p, stage)
            zi = zipfile.ZipInfo.from_file(p, arc)
            mode = 0o755 if os.access(p, os.X_OK) else 0o644
            zi.external_attr = (stat.S_IFREG | mode) << 16
            zi.compress_type = zipfile.ZIP_DEFLATED
            with open(p, "rb") as fh:
                z.writestr(zi, fh.read())
print("wrote", out)
EOF

# verify the archive
python3 - "$OUT" <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
names = z.namelist()
need = ["Echofield.sh", "echofield/echofield.aarch64", "echofield/port.json", "echofield/gameinfo.xml",
        "echofield/screenshot.png", "echofield/README.md", "echofield/assets/samples/glass_tines.wav"]
for n in need:
    assert n in names, "missing " + n
for n in ("Echofield.sh", "echofield/echofield.aarch64"):
    mode = (z.getinfo(n).external_attr >> 16) & 0o777
    assert mode & 0o111, n + " not executable"
assert not any(n.startswith("echofield/userdata") for n in names)
assert z.read("echofield/echofield.aarch64")[:4] == b"\x7fELF"
print("archive ok: %d files, %.1f MB" % (len(names), sum(i.file_size for i in z.infolist()) / 1e6))
EOF
echo "ECHOFIELD $VERSION packaged -> $OUT"
