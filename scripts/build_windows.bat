@echo off
rem Samsi Bassline - Build und Installation unter Windows 10/11 (x64)
rem
rem Voraussetzungen (einmalig):
rem   1. Visual Studio 2022 oder neuer (Community reicht) mit Workload
rem      "Desktopentwicklung mit C++": https://visualstudio.microsoft.com/de/downloads/
rem   2. CMake: https://cmake.org/download/  (bei der Installation "Add CMake to PATH" waehlen)
rem   3. Git fuer Windows: https://git-scm.com/download/win
rem
rem Aufruf: Rechtsklick auf diese Datei > "Als Administrator ausfuehren"
rem (Administratorrechte sind nur fuer das Kopieren nach C:\Program Files\Common Files\VST3 noetig.)

setlocal
cd /d "%~dp0\.."

where cmake >nul 2>nul || (echo FEHLER: cmake nicht gefunden. Bitte CMake installieren und "Add to PATH" waehlen. & pause & exit /b 1)
where git   >nul 2>nul || (echo FEHLER: git nicht gefunden. Bitte Git fuer Windows installieren. & pause & exit /b 1)

echo ==^> Konfigurieren (JUCE 9.0.4 wird beim ersten Mal automatisch geladen)
cmake -S . -B build -A x64 || (echo FEHLER beim Konfigurieren. Ist Visual Studio mit C++ installiert? & pause & exit /b 1)

echo ==^> Bauen (Release) - das erste Mal dauert einige Minuten
cmake --build build --config Release --target SamsiBassline_VST3 SamsiTests || (echo FEHLER beim Bauen. & pause & exit /b 1)

echo ==^> Automatische Tests
"build\SamsiTests_artefacts\Release\SamsiTests.exe"
if errorlevel 1 (echo WARNUNG: Mindestens ein Test ist fehlgeschlagen - siehe Ausgabe oben.)

set "SRC=build\SamsiBassline_artefacts\Release\VST3\Samsi Bassline.vst3"
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%SRC%" (echo FEHLER: "%SRC%" wurde nicht erzeugt. & pause & exit /b 1)

echo ==^> Installieren nach "%DEST%"
if not exist "%DEST%" mkdir "%DEST%"
if exist "%DEST%\Samsi Bassline.vst3" rmdir /s /q "%DEST%\Samsi Bassline.vst3"
xcopy /E /I /Y /Q "%SRC%" "%DEST%\Samsi Bassline.vst3" || (echo FEHLER: Kopieren fehlgeschlagen. Skript bitte "Als Administrator ausfuehren". & pause & exit /b 1)

echo.
echo Fertig. In Ableton Live: Options ^> Settings / Einstellungen ^> Plug-Ins ^> "Use VST3 Plug-in System Folders" = On,
echo dann "Rescan" / "Neu scannen" (mit gedrueckter Alt-Taste = vollstaendiger Neuscan).
pause
