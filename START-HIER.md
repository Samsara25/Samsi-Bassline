# Samsi Bassline – START HIER

Dieser Ordner ist das vollständige Plugin-Projekt (Quellcode, Build-Skripte, Tests, Presets, Dokumentation).
Daraus entsteht das **VST3-Plugin** für Windows und macOS.

## Weg A – Cloud-Build über GitHub (empfohlen, keine Installation nötig)

1. Kostenloses Konto auf **github.com** anlegen.
2. Oben rechts **„+“ → New repository** → Name `SamsiBassline`, **Private**, nichts ankreuzen → **Create repository**.
3. Auf der leeren Repository-Seite **„uploading an existing file“** klicken und den **Inhalt** dieses Ordners hineinziehen
   (alle Dateien und Ordner, auch den Ordner `.github` – im Finder/Explorer ggf. versteckte Dateien einblenden:
   macOS `Cmd + Shift + .`, Windows Explorer → Ansicht → Ausgeblendete Elemente). **Commit changes**.
4. Reiter **Actions** → „Build Samsi Bassline“ → **Run workflow** (läuft ohnehin automatisch nach dem Hochladen).
5. Nach ca. 15–30 Minuten den Lauf öffnen → unten unter **Artifacts**:
   - `SamsiBassline-Windows-x64` → enthält **Setup.exe**, ZIP und Anleitung
   - `SamsiBassline-macOS-Universal` → enthält **.pkg**, ZIP und Anleitung
6. Installieren und testen: `docs/INSTALLIEREN.md`.

Ist ein Schritt rot (Fehler), den Fehlertext kopieren und an Claude schicken – oder GitHub in claude.ai unter
Einstellungen → Connectors verbinden und den Repository-Namen nennen; dann kann Claude den Build selbst starten und Fehler beheben.

## Weg B – auf dem eigenen Rechner bauen

- **Windows:** Visual Studio 2022 Community („Desktopentwicklung mit C++“), CMake und Git installieren,
  dann `scripts\build_windows.bat` per Rechtsklick „Als Administrator ausführen“.
- **macOS:** Terminal: `xcode-select --install`, `brew install cmake`, dann im Ordner `bash scripts/build_macos.sh`.

Beide Skripte bauen das VST3, führen die automatischen Tests aus und installieren es in den VST3-Ordner.

## Was wo steht

| Datei / Ordner | Inhalt |
|---|---|
| `README.md` | Gesamtbeschreibung: Architektur, Funktionen, Tests, Routing in Ableton/Cubase/Bitwig/FL |
| `docs/PARAMETER.md` | alle Parameter mit ID, Bereich, Standardwert (Grundlage zum Weiterentwickeln) |
| `docs/PRESETS.md` | die 25 gemessenen Presets |
| `docs/INSTALLIEREN.md` | Installation und Schnelltest |
| `Source/` | C++-Quellcode (JUCE 9, CMake) – `Parameters.*` = alle Regler, `dsp/` = Klang, `ui/` = Oberfläche, `fit/` = Auto Fit |
| `tests/TestMain.cpp` | automatische Tests (295 Prüfungen) |
| `.github/workflows/build.yml` | Cloud-Build Windows + macOS inkl. Installer |
| `installer/` | Installer-Skripte (Inno Setup für Windows, pkgbuild für macOS) |

**Stand:** Linux, Windows x64 und macOS Universal gebaut, alle 295 automatischen Tests bestanden; Installer werden im
Cloud-Build erzeugt. Noch offen: Test in Ableton/Cubase/Bitwig/FL Studio auf echten Rechnern.
