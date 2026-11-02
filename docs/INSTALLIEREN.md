# Samsi Bassline installieren und testen

## Windows 10/11 (64 Bit)

1. `SamsiBassline-…-Windows-x64-Setup.exe` doppelklicken.
   Erscheint „Der Computer wurde durch Windows geschützt“: **Weitere Informationen → Trotzdem ausführen**
   (der Installer ist nicht mit einem gekauften Zertifikat signiert).
2. Der Installer legt `Samsi Bassline.vst3` nach `C:\Program Files\Common Files\VST3`.
   Ohne Installer: den Ordner `Samsi Bassline.vst3` aus der ZIP-Datei selbst dorthin kopieren.
3. Deinstallieren: Windows-Einstellungen → Apps → „Samsi Bassline (VST3)“.

## macOS (Apple Silicon und Intel, ab macOS 10.15)

1. `SamsiBassline-…-macOS-Universal.pkg` doppelklicken. Meldet macOS, dass der Entwickler nicht verifiziert ist:
   **Systemeinstellungen → Datenschutz & Sicherheit → „Dennoch öffnen“** (das Paket ist nicht von Apple notarisiert).
2. Das Paket legt `Samsi Bassline.vst3` nach `/Library/Audio/Plug-Ins/VST3`.
   Ohne Installer: den Ordner aus der ZIP-Datei nach `~/Library/Audio/Plug-Ins/VST3` kopieren und einmal im Terminal
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Samsi Bassline.vst3"` ausführen.

## In Ableton Live 12

1. Einstellungen öffnen (Windows `Strg + ,`, macOS `Cmd + ,`) → Reiter **Plug-Ins**.
2. **Use VST3 Plug-in System Folders** einschalten.
3. **Rescan** – bei einer neuen Version mit gedrückter **Alt** (Windows) / **Option** (macOS) für einen vollständigen Neuscan.
4. Browser → **Plug-Ins → VST3 → Samsi Audio → Samsi Bassline** auf eine MIDI-Spur ziehen, Play drücken.

Andere DAWs lesen dieselben Ordner: Cubase (Studio → VST-PlugIn-Manager → neu scannen), Bitwig (Einstellungen → Plug-ins),
FL Studio (Options → Manage plugins → Find plugins).

## Schnelltest (5 Minuten)

- [ ] Plugin lädt als Instrument, Fenster öffnet sich, Größe lässt sich ziehen.
- [ ] Play: Kick und Bass spielen K-B-B-B im Projekttempo; LIVE-Ansicht zeigt beide Wellenformen.
- [ ] Preset-Menü: ein paar Presets durchklicken.
- [ ] Orb in der Mitte klicken → Auto Fit läuft, danach pulsiert das Orb (Phase im Ziel).
- [ ] Einen Regler automatisieren (z. B. Bass Cutoff) – Automation wird abgespielt.
- [ ] Set speichern, schließen, wieder öffnen → alles ist gleich.

Fehler oder Abstürze bitte mit DAW-Version, Betriebssystem und kurzem Ablauf melden.
