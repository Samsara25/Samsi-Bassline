#!/bin/bash
# macOS-Installer (.pkg) – wird im Cloud-Build automatisch erzeugt.
# Installiert das VST3 nach /Library/Audio/Plug-Ins/VST3 (für alle Benutzer, alle DAWs).
set -euo pipefail
VERSION="${1:-0.5.0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/build/SamsiBassline_artefacts/Release/VST3/Samsi Bassline.vst3"
STAGE="$(mktemp -d)"
mkdir -p "$STAGE/payload" "$ROOT/dist"
cp -R "$SRC" "$STAGE/payload/"
# lokale Ad-hoc-Signatur (nötig für Apple Silicon; kein Entwicklerkonto)
codesign --force --deep --sign - "$STAGE/payload/Samsi Bassline.vst3"
pkgbuild --root "$STAGE/payload" \
         --identifier com.samsiaudio.samsibassline.vst3 \
         --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         "$ROOT/dist/SamsiBassline-$VERSION-macOS-Universal.pkg"
echo "Erstellt: dist/SamsiBassline-$VERSION-macOS-Universal.pkg"
