"""Kalibriert die 25 Preset-Entwürfe gegen die Referenz-Zielwerte.

Ablauf je Runde: Preset mit dem echten Plugin-Code rendern (SamsiTests --render, getrennte
Kick-/Bass-Spuren), mit stems.py messen (gleiche Definitionen wie bei den Referenztracks),
dann wenige Parameter nachregeln:
  Kick-Länge t20  -> kick_decay_ms        Click-Anteil -> kick_click_level
  Bassnoten-Länge -> bass_dec / lenPct    Bass-Neigung -> bass_cutoff
Die Pitch-Kurve kommt analytisch aus design.py und wird nur gemessen.
"""
import json, os, sys, subprocess, math, copy
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stems import measure_stems

ROOT = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(ROOT, '..', 'build', 'SamsiTests_artefacts', 'Release', 'SamsiTests')
OUT = os.path.join(ROOT, 'renders')   # Zwischendateien (nicht im Paket)
os.makedirs(OUT, exist_ok=True)

TOL = dict(t20=0.12, len=0.15, click=3.0, tilt1=1.5)  # relative bzw. dB

def render_measure(i, p, bars=10):
    js = os.path.join(OUT, f'p{i:02d}.json')
    json.dump(dict(bpm=p['bpm'], params=p['params'], pattern=p['pattern']), open(js, 'w'))
    wav = os.path.join(OUT, f'p{i:02d}.wav')
    r = subprocess.run([BIN, '--render', js, wav, str(bars)], capture_output=True, text=True)
    if 'unbekannter' in r.stdout: print(p['name'], r.stdout)
    peak = float(r.stdout.split('peak')[1].split('dB')[0]) if 'peak' in r.stdout else 0
    m = measure_stems(wav[:-4], p['bpm'])
    m['peak_db'] = peak
    return m

def adjust(p, m):
    t = p['targets']; q = p['params']
    k, b = m['kick'], m['bass']
    if k['t20_ms'] > 0:
        r = t['t20'] / k['t20_ms']
        q['kick_decay_ms'] = round(min(1500, max(30, q['kick_decay_ms'] * r ** 1.1)), 1)
        q['kick_tail_ms'] = round(min(1500, max(10, q['kick_tail_ms'] * r)), 1)
    end = q['kick_end_hz']
    if k['f_at_25'] > end * 1.05 and t['f25'] > end * 1.05:
        r = math.log(t['f25'] / end) / math.log(k['f_at_25'] / end)
        q['kick_pitch_ms'] = round(min(500, max(20, q['kick_pitch_ms'] * r ** 0.5)), 1)
    q['kick_click_level'] = round(min(0, max(-40, q['kick_click_level'] + 0.8 * (t['click'] - k['click_rel_db']))), 1)
    if k['click_rel_db'] > t['click'] + 2 and q['kick_click_level'] <= -38:
        if q['kick_drive'] > 0:
            q['kick_drive'] = max(0, q['kick_drive'] - 2)      # Click kommt aus dem Körper -> weniger Sättigung
        else:
            q['kick_attack_ms'] = round(min(6.0, q.get('kick_attack_ms', 0.3) + 0.8), 2)   # weicherer Einschwinger
    if k['f_at_50'] > end * 1.02 and t['f50'] > end * 1.05 and abs(k['f_at_25'] / t['f25'] - 1) < 0.15:
        r = math.log(k['f_at_50'] / end) / math.log(t['f50'] / end)   # >1: zu hoch -> Kurve steiler
        q['kick_pitch_curve'] = round(min(8.0, max(0.3, q['kick_pitch_curve'] * r ** 0.35)), 3)
    if b['note_len_ms'] > 0:
        r = t['len'] / b['note_len_ms']
        nd = q['bass_dec'] * r ** 1.2
        q['bass_dec'] = round(min(400, max(15, nd)), 1)
        if (nd > 400 and r > 1) or (nd < 15 and r < 1):            # zweiter Hebel: Sustain und Gate-Länge
            q['bass_sus'] = round(min(0.9, max(0.05, q['bass_sus'] * r)), 3)
            for st in p['pattern']: st[5] = int(min(100, max(30, st[5] * r ** 0.5)))
    d = t['tilt1'] - b['tilt_120_250']
    nc = q['bass_cutoff'] * 2 ** (d / 5.0)
    q['bass_cutoff'] = round(min(2500, max(260, nc)), 1)
    if nc > 2500 or nc < 260:                                       # zweiter Hebel: Sub-Anteil
        ns = q.get('bass_sub', 0.5) * 10 ** (-d / 20)
        q['bass_sub'] = round(min(0.9, max(0.1, ns)), 3)
        if ns > 0.9 or ns < 0.1:                                    # dritter Hebel: Wavetable-Anteil
            q['bass_wt_level'] = round(min(1.0, max(0.1, q.get('bass_wt_level', 0.5) * 10 ** (d / 20))), 3)

def within(p, m):
    t = p['targets']; k, b = m['kick'], m['bass']
    return dict(
        f50=abs(k['f_at_50'] / t['f50'] - 1) <= 0.15,
        f25=abs(k['f_at_25'] / t['f25'] - 1) <= 0.12,
        t20=abs(k['t20_ms'] / t['t20'] - 1) <= TOL['t20'],
        len=abs(b['note_len_ms'] / t['len'] - 1) <= TOL['len'],
        click=abs(k['click_rel_db'] - t['click']) <= TOL['click'],
        tilt1=abs(b['tilt_120_250'] - t['tilt1']) <= TOL['tilt1'])

def main(rounds=5):
    src = 'presets_calibrated.json' if os.environ.get('CONTINUE') else 'presets_design.json'
    P = json.load(open(os.path.join(ROOT, src)))
    for rnd in range(rounds):
        with ThreadPoolExecutor(8) as ex:
            M = list(ex.map(lambda a: render_measure(*a), enumerate(P)))
        ok = [within(p, m) for p, m in zip(P, M)]
        n_ok = sum(all(o.values()) for o in ok)
        print(f'Runde {rnd + 1}: {n_ok}/25 vollständig im Zielbereich')
        if n_ok == 25 or rnd == rounds - 1: break
        for p, m, o in zip(P, M, ok):
            if not all(o.values()): adjust(p, m)
    for p, m, o in zip(P, M, ok):
        p['measured'] = dict(t20=round(m['kick']['t20_ms'], 1), f5=m['kick']['f_at_5'], f25=m['kick']['f_at_25'], f50=m['kick']['f_at_50'],
                             f0=round(m['kick']['f0_hz'], 2), click=round(m['kick']['click_rel_db'], 1),
                             len=round(m['bass']['note_len_ms'], 1), tilt1=round(m['bass']['tilt_120_250'], 2),
                             tilt2=round(m['bass']['tilt_250_1k'], 2), centroid=round(m['bass']['centroid_hz'], 1), peak=m['peak_db'])
        p['ok'] = o
    json.dump(P, open(os.path.join(ROOT, 'presets_calibrated.json'), 'w'), indent=1, ensure_ascii=False, default=lambda o: o.item())
    for p in P:
        me, t = p['measured'], p['targets']
        flag = ''.join('✓' if v else '✗' for v in p['ok'].values())
        print(f"{p['name']:26s} {flag} t20 {me['t20']:6.1f}/{t['t20']:<4} f25 {me['f25']:6.1f}/{t['f25']:<4} f50 {me['f50']:6.1f}/{t['f50']:<4} "
              f"click {me['click']:6.1f}/{t['click']:<4} len {me['len']:5.1f}/{t['len']:<3} tilt {me['tilt1']:5.1f}/{t['tilt1']:<5} f0 {me['f0']:5.1f} pk {me['peak']:5.1f}")

if __name__ == '__main__':
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 5)
