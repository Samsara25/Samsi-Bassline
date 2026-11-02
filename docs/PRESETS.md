# Werks-Presets: Zielwerte aus den Referenztracks und Messergebnis

Jedes Preset wurde mit dem Plugin-Code gerendert (44,1 kHz, 10 Takte, getrennte Kick-/Bass-Spur) und mit
denselben Messdefinitionen ausgewertet wie die sechs Referenztracks (`presets/calibrate.py`).
Toleranzen: Kick-Länge ±12 %, Tonhöhe bei 25 ms ±12 %, bei 50 ms ±15 %, Click ±3 dB, Bassnoten-Länge ±15 %, Bass-Neigung ±1,5 dB.

**Messgrößen:** t20 = Zeit bis der Kick 20 dB unter seinem Maximum liegt · f25/f50 = Kick-Tonhöhe nach 25/50 ms (Nulldurchgänge, Tiefpass 700 Hz) ·
Click = Energie über 2 kHz relativ zu unter 300 Hz in den ersten 6 ms · Länge = Bassnote über −12 dB ·
Neigung = Bass-Energie 120–250 Hz relativ zu 60–120 Hz.

| # | Preset | Thema | Tempo / Tonart / Raster | t20 ms (Ziel) | f25 Hz | f50 Hz | Click dB | Bassnote ms | Neigung dB | im Ziel |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Rostige Mantra-Maschine | Industrial | 200 BPM · F# Phrygisch · 1/16 K-B-B-B | 106 (105) | 167 (150) | 86 (100) | -1.0 (-1) | 56 (62) | -5.5 (-5.8) | 6/6 |
| 2 | Stahlkathedrale | Industrial | 196 BPM · G Phryg.-Dominant · 1/16 K-B-B-B | 104 (105) | 165 (150) | 90 (100) | -1.0 (-1) | 58 (62) | -6.0 (-5.8) | 6/6 |
| 3 | Kolben im Kosmos | Industrial | 200 BPM · E Phrygisch · 1/16 K-B-.-B | 105 (105) | 165 (150) | 90 (100) | -1.0 (-1) | 60 (62) | -5.8 (-5.8) | 6/6 |
| 4 | Nachtschatten-Nebula | Psy-Fi Dark | 180 BPM · D Phrygisch · 1/16 Triole Triolen-Roll K-.-B-B-B-B | 97 (92) | 113 (104) | 68 (74) | -8.5 (-9) | 48 (47) | -5.5 (-4.7) | 6/6 |
| 5 | Mondfresser | Psy-Fi Dark | 182 BPM · C# Moll · 1/16 Triole Triolen-Galopp K-.-B-.-B-B | 93 (92) | 113 (104) | 68 (74) | -8.7 (-9) | 50 (47) | -5.4 (-4.7) | 6/6 |
| 6 | Schwarzlicht-Sutra | Psy-Fi Dark | 180 BPM · A Phryg.-Dominant · 1/16 Triole Triolen-Roll K-.-B-B-B-B | 89 (92) | 112 (104) | 71 (74) | -9.0 (-9) | 48 (47) | -6.1 (-4.7) | 6/6 |
| 7 | Spektrale Sümpfe | Psy-Fi Dark | 184 BPM · F Moll · 1/16 Triole Triolen-Roll K-.-B-B-B-B | 99 (92) | 112 (104) | 69 (74) | -8.0 (-9) | 47 (46) | -4.1 (-4.7) | 6/6 |
| 8 | Chrom-Prophet | Futuristic | 200 BPM · G# Moll · 1/16 K-B-B-B | 108 (110) | 133 (125) | 82 (82) | -3.0 (-3) | 58 (60) | -4.9 (-5.2) | 6/6 |
| 9 | Quantensamen | Futuristic | 198 BPM · A# Dorisch · 1/16 K-B-B-B | 108 (110) | 134 (125) | 79 (82) | -3.0 (-3) | 58 (60) | -4.3 (-5.2) | 6/6 |
| 10 | Neon-Nirvana | Futuristic | 192 BPM · F# Moll · 1/16 K-.-B-B | 110 (110) | 134 (125) | 82 (82) | -2.5 (-3) | 64 (60) | -4.2 (-5.2) | 6/6 |
| 11 | Xenomantra | Alien | 185 BPM · E Phryg.-Dominant · 1/16 Triole Triolen-Roll K-.-B-B-B-B | 115 (118) | 122 (118) | 78 (80) | -10.0 (-10) | 44 (46) | -6.7 (-7.0) | 6/6 |
| 12 | Sternsaat-Signal | Alien | 186 BPM · G Phrygisch · 1/16 Triole Triolen-Galopp K-.-B-.-B-B | 122 (118) | 126 (118) | 82 (80) | -8.4 (-10) | 41 (46) | -7.7 (-7.0) | 6/6 |
| 13 | Tentakel-Telepathie | Alien | 188 BPM · D# Harm. Moll · 1/16 Triole Triolen-Roll K-.-B-B-B-B | 122 (118) | 126 (118) | 82 (80) | -9.3 (-10) | 41 (45) | -7.5 (-7.0) | 6/6 |
| 14 | Biomechanischer Lotus | Biomech | 190 BPM · H Phrygisch · 1/16 K-.-B-B | 144 (145) | 105 (98) | 71 (75) | -20.5 (-18) | 66 (67) | -2.1 (-2.5) | 6/6 |
| 15 | Wirbelknochen-Orgel | Biomech | 188 BPM · C Phryg.-Dominant · 1/16 K-.-B-B | 141 (145) | 110 (98) | 67 (75) | -19.5 (-18) | 66 (68) | -2.5 (-2.5) | 6/6 |
| 16 | Nekro-Kathedrale | Biomech | 190 BPM · A Moll · 1/16 K-B-.-B | 148 (145) | 108 (98) | 64 (75) | -18.6 (-18) | 73 (67) | -3.0 (-2.5) | 6/6 |
| 17 | Wurzelgeist | Holzig | 185 BPM · G Phrygisch · 1/16 K-.-B-B | 128 (125) | 112 (110) | 67 (68) | -4.9 (-4) | 57 (55) | -4.2 (-4.5) | 6/6 |
| 18 | Rindenrune | Holzig | 183 BPM · F# Moll · 1/16 K-B-B-B | 129 (125) | 108 (110) | 68 (68) | -6.1 (-4) | 51 (55) | -3.6 (-4.5) | 6/6 |
| 19 | Moosorakel | Holzig | 185 BPM · A Dorisch · 1/16 K-.-B-B | 133 (125) | 108 (110) | 68 (68) | -5.8 (-4) | 59 (55) | -5.8 (-4.5) | 6/6 |
| 20 | Morgenkristall | Future | 185 BPM · G# Dur · 1/16 K-.-B-B | 128 (128) | 110 (104) | 69 (72) | -8.0 (-8) | 65 (62) | -5.1 (-3.8) | 6/6 |
| 21 | Lichtgitter | Future | 188 BPM · H Moll · 1/16 K-B-B-B | 128 (128) | 105 (104) | 73 (72) | -8.0 (-8) | 55 (62) | -5.2 (-3.8) | 6/6 |
| 22 | Zeitlinien-Zauber | Future | 190 BPM · E Moll · 1/16 K-.-B-B | 133 (128) | 111 (104) | 68 (72) | -6.4 (-8) | 60 (62) | -4.2 (-3.8) | 6/6 |
| 23 | Hyperlotus 214 | High Tech | 214 BPM · G Phrygisch · 1/16 Triole Triolen-Roll K-.-B-B-B-B | 100 (100) | 127 (118) | 76 (88) | -10.7 (-13) | 31 (36) | -7.0 (-6.5) | 6/6 |
| 24 | Synapsen-Sturm | High Tech | 210 BPM · F# Phryg.-Dominant · 1/32 1/32-Roll (spät) | 98 (100) | 130 (118) | 82 (88) | -10.4 (-13) | 29 (30) | -5.1 (-6.5) | 6/6 |
| 25 | Fraktal-Feuer | High Tech | 205 BPM · A Harm. Moll · 1/16 Triole Triolen-Galopp K-.-B-.-B-B | 103 (100) | 126 (118) | 83 (88) | -10.5 (-13) | 37 (36) | -6.4 (-6.5) | 6/6 |

## Auto Fit je Preset (48 kHz, Preset-Tempo, internes Pattern)

Die gefundenen Fit-Werte sind im Preset gespeichert. „Auslöschung max“ = schlimmster 10-ms-Abschnitt im Band 20–60 Hz,
in dem die Summe leiser ist als die Leistungsaddition von Kick und Bass. Der Orb in der Mitte pulsiert, wenn dieser Wert unter 3 dB liegt
und die Tonart-Prüfung grün ist.

| Preset | Auslöschung 20–60 Hz max (vorher → nachher) | Einsatz-Auslöschung dB | Summenpeak dBFS | Zielfunktion |
|---|---|---|---|---|
| Rostige Mantra-Maschine | 6.47 → 2.43 | 0.11 → 0.00 | -4.0 → -4.0 | 11.43 → 1.32 |
| Stahlkathedrale | 3.73 → 2.09 | 0.00 → 0.14 | -4.0 → -4.0 | 11.99 → 1.50 |
| Kolben im Kosmos | 12.78 → 2.52 | 0.00 → 0.00 | -3.9 → -3.9 | 31.87 → 1.41 |
| Nachtschatten-Nebula | 11.82 → 1.84 | 0.53 → 0.04 | -3.9 → -3.9 | 24.50 → 0.87 |
| Mondfresser | 9.87 → 1.83 | 0.00 → 0.00 | -3.8 → -3.8 | 15.62 → 0.60 |
| Schwarzlicht-Sutra | 4.09 → 1.17 | 1.75 → 0.00 | -4.0 → -4.0 | 7.71 → 0.91 |
| Spektrale Sümpfe | 0.74 → 0.52 | 0.02 → 0.01 | -3.8 → -3.8 | 0.14 → 0.10 |
| Chrom-Prophet | 9.14 → 2.11 | 0.11 → 0.49 | -4.0 → -4.0 | 48.18 → 1.12 |
| Quantensamen | 4.76 → 0.24 | 0.53 → 0.08 | -3.9 → -3.9 | 11.45 → 0.14 |
| Neon-Nirvana | 0.00 → 0.00 | 0.00 → 0.00 | -3.9 → -3.9 | 0.00 → 0.00 |
| Xenomantra | 4.88 → 1.63 | 0.01 → 0.30 | -4.1 → -4.1 | 34.59 → 0.55 |
| Sternsaat-Signal | 1.82 → 0.48 | 0.65 → 0.73 | -4.1 → -4.1 | 1.31 → 0.89 |
| Tentakel-Telepathie | 12.32 → 1.09 | 0.01 → 0.48 | -4.0 → -4.0 | 53.42 → 0.64 |
| Biomechanischer Lotus | 3.55 → 0.46 | 2.99 → 0.14 | -4.9 → -4.9 | 15.54 → 0.49 |
| Wirbelknochen-Orgel | 2.14 → 0.53 | 1.78 → 0.00 | -4.7 → -4.7 | 5.81 → 0.06 |
| Nekro-Kathedrale | 4.67 → 1.72 | 0.00 → 0.39 | -4.8 → -4.8 | 7.66 → 1.34 |
| Wurzelgeist | 2.22 → 0.00 | 0.03 → 0.00 | -3.3 → -3.3 | 5.61 → 0.00 |
| Rindenrune | 0.21 → 0.21 | 0.80 → 0.20 | -3.5 → -3.5 | 2.64 → 0.17 |
| Moosorakel | 0.49 → 0.00 | 0.00 → 0.00 | -3.4 → -3.4 | 0.16 → 0.00 |
| Morgenkristall | 2.34 → 0.66 | 0.00 → 0.13 | -4.1 → -4.1 | 0.92 → 0.12 |
| Lichtgitter | 0.00 → 0.53 | 1.03 → 0.49 | -4.2 → -4.2 | 1.59 → 0.40 |
| Zeitlinien-Zauber | 8.60 → 0.44 | 2.00 → 0.08 | -4.0 → -4.0 | 18.52 → 0.18 |
| Hyperlotus 214 | 2.46 → 0.54 | 0.51 → 0.53 | -4.2 → -4.2 | 1.38 → 0.61 |
| Synapsen-Sturm | 4.08 → 1.67 | 1.53 → 0.01 | -4.2 → -4.2 | 6.06 → 0.70 |
| Fraktal-Feuer | 5.25 → 0.91 | 0.00 → 0.36 | -4.2 → -4.2 | 73.87 → 0.66 |

## Bezug der Themen zu den Referenzen

- **Industrial** – Zielwerte abgeleitet aus: Obscurum & Logic Psycho - Andromeda (200)
- **Psy-Fi Dark** – Zielwerte abgeleitet aus: Holy Manullu & Obscurum feat. Samsara - NN (180)
- **Futuristic** – Zielwerte abgeleitet aus: Andromeda (200) / Critical Freak (185)
- **Alien** – Zielwerte abgeleitet aus: ein unze (185)
- **Biomech** – Zielwerte abgeleitet aus: vs Manullu (190)
- **Holzig** – Zielwerte abgeleitet aus: Critical Freak - Back (185)
- **Future** – Zielwerte abgeleitet aus: Critical Freak (185) / vs Manullu (190)
- **High Tech** – Zielwerte abgeleitet aus: psycore 214

Kick-Grundton und Bass-Grundton liegen in jedem Preset auf derselben Tonhöhenklasse (Key-Ansicht: grün).
Bei 1/32-Rastern ist die Ziel-Notenlänge auf 85 % der Schrittlänge begrenzt, weil die nächste Note sonst überlappt.
Die Bass-Zielwerte der Referenzen wurden aus dem fertigen Mix in den Bass-Slots gemessen (dort können auch Hi-Hats und
andere Spuren mitklingen); sie sind deshalb Richtwerte und kein exaktes Klangziel.
