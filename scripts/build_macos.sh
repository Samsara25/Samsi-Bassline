#!/usr/bin/env bash
# Samsi Bassline – Build und Installation auf macOS (Apple Silicon + Intel, Universal Binary)
#
# Voraussetzungen (einmalig):
#   1. Xcode aus dem App Store installieren und einmal starten
#      (oder nur die Command Line Tools:  xcode-select --install)
#   2. CMake installieren: https://cmake.org/download/  (oder: brew install cmake)
#
# Aufruf im Projektordner:
#   bash scripts/build_macos.sh            # bauen + Tests + nach ~/Library/Audio/Plug-Ins/VST3 kopieren
#   bash scripts/build_macos.sh --no-tests # ohne Tests

set -euo pipefail
cd "$(dirname "$0")/.."

RUN_TESTS=1
[[ "${1:-}" == "--no-tests" ]] && RUN_TESTS=0

command -v cmake >/dev/null || { echo "FEHLER: cmake nicht gefunden. Bitte CMake installieren (https://cmake.org/download/)."; exit 1; }
command -v git   >/dev/null || { echo "FEHLER: git nicht gefunden. Bitte 'xcode-select --install' ausfuehren."; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "FEHLER: Xcode Command Line Tools fehlen. Bitte 'xcode-select --install' ausfuehren."; exit 1; }

echo "==> Konfigurieren (JUCE 9.0.4 wird beim ersten Mal automatisch geladen)"
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15

TARGETS=(SamsiBassline_VST3)
[[ $RUN_TESTS == 1 ]] && TARGETS+=(SamsiTests)

echo "==> Bauen (Release) – das erste Mal dauert einige Minuten"
cmake --build build --config Release --target "${TARGETS[@]}" -- -quiet

PLUGIN="build/SamsiBassline_artefacts/Release/VST3/Samsi Bassline.vst3"
[[ -d "$PLUGIN" ]] || { echo "FEHLER: $PLUGIN wurde nicht erzeugt."; exit 1; }

if [[ $RUN_TESTS == 1 ]]; then
  echo "==> Automatische Tests"
  "build/SamsiTests_artefacts/Release/SamsiTests"
fi

DEST="$HOME/Library/Audio/Plug-Ins/VST3"
echo "==> Installieren nach $DEST"
mkdir -p "$DEST"
rm -rf "$DEST/Samsi Bassline.vst3"
cp -R "$PLUGIN" "$DEST/"
# lokale Ad-hoc-Signatur (nötig für Apple Silicon; kein Apple-Developer-Konto erforderlich)
codesign --force --deep --sign - "$DEST/Samsi Bassline.vst3"
xattr -dr com.apple.quarantine "$DEST/Samsi Bassline.vst3" 2>/dev/null || true

lipo -info "$DEST/Samsi Bassline.vst3/Contents/MacOS/Samsi Bassline" || true
echo
echo "Fertig. In Ableton Live: Settings/Einstellungen > Plug-Ins > 'Use VST3 Plug-in System Folders' = On,"
echo "dann 'Rescan' / 'Neu scannen' (mit gedrueckter Wahltaste = vollstaendiger Neuscan)."
