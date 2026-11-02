"""Erzeugt Source/content/PresetData.inc und docs/PRESETS.md aus presets_calibrated.json."""
import json, os
ROOT = os.path.dirname(os.path.abspath(__file__))
P = json.load(open(os.path.join(ROOT, 'presets_calibrated.json')))
# Auto-Fit-Ergebnisse (SamsiTests --fit-presets) werden in die Presets übernommen:
# jedes Preset startet bereits phasen-abgestimmt.
FIT = {}
fp = os.path.join(ROOT, 'fit_results.json')
if os.path.exists(fp):
    FIT = {f['name']: f for f in json.load(open(fp))}
for p in P:
    f = FIT.get(p['name'])
    if f:
        p['params'].update({k: v for k, v in f['fit'].items()})
        p['fit_before'], p['fit_after'] = f['before'], f['after']
GRID = ['1/16', '1/16 Triole', '1/32']
KIND = {'KBBB': 'K-B-B-B', 'KxBB': 'K-.-B-B', 'KBxB': 'K-B-.-B', 'TRIP': 'Triolen-Roll K-.-B-B-B-B',
        'TRIPGAL': 'Triolen-Galopp K-.-B-.-B-B', 'ROLL32': '1/32-Roll', 'ROLL32B': '1/32-Roll (spät)'}

def cf(v):
    t = f'{float(v):.6g}'
    return (t if ('.' in t or 'e' in t) else t + '.0') + 'f'

def cstr(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

out = ['// Automatisch erzeugt von presets/gen_cpp.py – nicht von Hand bearbeiten.',
       '// Quelle: presets/presets_calibrated.json (Entwurf design.py, Kalibrierung calibrate.py).',
       '// Format: { Name, Thema, Hinweis, { {Parameter-ID, Klarwert}, ... }, { 32 Schritte: kick,kickVel,bass,bassVel,note,lenPct,slide } }', '']
for p in P:
    q = p['params']
    hint = f"{p['bpm']} BPM · {p['key']} · {GRID[q['seq_grid']]} {KIND[p['pattern_kind']]}"
    vals = ', '.join(f'{{ {cstr(k)}, {cf(v)} }}' for k, v in q.items())
    steps = ', '.join('{' + ','.join(str(int(x)) for x in s) + '}' for s in p['pattern'])
    out.append(f'{{ {cstr(p["name"])}, {cstr(p["theme"])}, {cstr(hint)},\n  {{ {vals} }},\n  {{ {steps} }} }},')
open(os.path.join(ROOT, '..', 'Source', 'content', 'PresetData.inc'), 'w').write('\n'.join(out) + '\n')

# ---------------------------------------------------------------- Messbericht
md = ['# Werks-Presets: Zielwerte aus den Referenztracks und Messergebnis', '',
      'Jedes Preset wurde mit dem Plugin-Code gerendert (44,1 kHz, 10 Takte, getrennte Kick-/Bass-Spur) und mit',
      'denselben Messdefinitionen ausgewertet wie die sechs Referenztracks (`presets/calibrate.py`).',
      'Toleranzen: Kick-Länge ±12 %, Tonhöhe bei 25 ms ±12 %, bei 50 ms ±15 %, Click ±3 dB, Bassnoten-Länge ±15 %, Bass-Neigung ±1,5 dB.', '',
      '**Messgrößen:** t20 = Zeit bis der Kick 20 dB unter seinem Maximum liegt · f25/f50 = Kick-Tonhöhe nach 25/50 ms (Nulldurchgänge, Tiefpass 700 Hz) ·',
      'Click = Energie über 2 kHz relativ zu unter 300 Hz in den ersten 6 ms · Länge = Bassnote über −12 dB ·',
      'Neigung = Bass-Energie 120–250 Hz relativ zu 60–120 Hz.', '',
      '| # | Preset | Thema | Tempo / Tonart / Raster | t20 ms (Ziel) | f25 Hz | f50 Hz | Click dB | Bassnote ms | Neigung dB | im Ziel |',
      '|---|---|---|---|---|---|---|---|---|---|---|']
for i, p in enumerate(P, 1):
    m, t = p['measured'], p['targets']
    q = p['params']
    ok = sum(p['ok'].values()); n = len(p['ok'])
    md.append(f"| {i} | {p['name']} | {p['theme']} | {p['bpm']} BPM · {p['key']} · {GRID[q['seq_grid']]} {KIND[p['pattern_kind']]} | "
              f"{m['t20']:.0f} ({t['t20']}) | {m['f25']:.0f} ({t['f25']}) | {m['f50']:.0f} ({t['f50']}) | {m['click']:.1f} ({t['click']}) | "
              f"{m['len']:.0f} ({t['len']:.0f}) | {m['tilt1']:.1f} ({t['tilt1']}) | {ok}/{n} |")
md += ['', '## Auto Fit je Preset (48 kHz, Preset-Tempo, internes Pattern)', '',
       'Die gefundenen Fit-Werte sind im Preset gespeichert. „Auslöschung max“ = schlimmster 10-ms-Abschnitt im Band 20–60 Hz,',
       'in dem die Summe leiser ist als die Leistungsaddition von Kick und Bass. Der Orb in der Mitte pulsiert, wenn dieser Wert unter 3 dB liegt',
       'und die Tonart-Prüfung grün ist.', '',
       '| Preset | Auslöschung 20–60 Hz max (vorher → nachher) | Einsatz-Auslöschung dB | Summenpeak dBFS | Zielfunktion |',
       '|---|---|---|---|---|']
for p in P:
    if 'fit_after' in p:
        b, a = p['fit_before'], p['fit_after']
        md.append(f"| {p['name']} | {b['cancel_worst_20_60']:.2f} → {a['cancel_worst_20_60']:.2f} | {b['onset_cancel']:.2f} → {a['onset_cancel']:.2f} | "
                  f"{b['sum_peak']:.1f} → {a['sum_peak']:.1f} | {b['cost']:.2f} → {a['cost']:.2f} |")
md += ['', '## Bezug der Themen zu den Referenzen', '']
seen = set()
for p in P:
    if p['theme'] in seen: continue
    seen.add(p['theme'])
    md.append(f"- **{p['theme']}** – Zielwerte abgeleitet aus: {p['reference']}")
md += ['', 'Kick-Grundton und Bass-Grundton liegen in jedem Preset auf derselben Tonhöhenklasse (Key-Ansicht: grün).',
       'Bei 1/32-Rastern ist die Ziel-Notenlänge auf 85 % der Schrittlänge begrenzt, weil die nächste Note sonst überlappt.',
       'Die Bass-Zielwerte der Referenzen wurden aus dem fertigen Mix in den Bass-Slots gemessen (dort können auch Hi-Hats und',
       'andere Spuren mitklingen); sie sind deshalb Richtwerte und kein exaktes Klangziel.']
open(os.path.join(ROOT, '..', 'docs', 'PRESETS.md'), 'w').write('\n'.join(md) + '\n')
print('PresetData.inc und docs/PRESETS.md geschrieben:', len(P), 'Presets')
