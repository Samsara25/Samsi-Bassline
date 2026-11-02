"""Entwurf der 25 Werks-Presets aus den Referenzmessungen.

Jeder Stil (Thema) hat Zielwerte, die aus den Referenztracks gemessen wurden
(siehe docs/REFERENZEN.md). Kick und Bass eines Presets stehen immer auf demselben
Grundton (Kick-Grundton = Bass-Grundton), damit Auto Fit stabile Phasen findet.
Die Klangparameter hier sind Startwerte; calibrate.py rendert jedes Preset mit dem
Plugin und regelt Kick-Länge, Pitch-Verlauf, Click, Notenlänge und Bass-Helligkeit
nach, bis die Messwerte im Zielbereich liegen.
"""
import json, math

NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'H']
SCALES = {'moll': 0, 'phryg': 1, 'harm': 2, 'phrygdom': 3, 'dur': 4, 'dor': 5}
INTERVALS = {0: [0, 2, 3, 5, 7, 8, 10], 1: [0, 1, 3, 5, 7, 8, 10], 2: [0, 2, 3, 5, 7, 8, 11],
             3: [0, 1, 4, 5, 7, 8, 10], 4: [0, 2, 4, 5, 7, 9, 11], 5: [0, 2, 3, 5, 7, 9, 10]}

def hz(midi): return 440.0 * 2 ** ((midi - 69) / 12)

# Zielwerte je Thema (Messgrößen siehe stems.py) – abgeleitet aus den Referenzen:
#   t20: Kick-Länge bis -20 dB, f25/f50: Kick-Tonhöhe nach 25/50 ms, click: Click-Anteil,
#   len: Bassnoten-Länge (-12 dB), tilt1/tilt2: Bass-Neigung 120-250 / 250-1k Hz zu 60-120 Hz
THEMES = {
    'Industrial':  dict(ref='Obscurum & Logic Psycho - Andromeda (200)', t20=105, f25=150, f50=100, click=-1,  len=62, tilt1=-5.8, tilt2=-7.0),
    'Psy-Fi Dark': dict(ref='Holy Manullu & Obscurum feat. Samsara - NN (180)', t20=92,  f25=104, f50=74, click=-9, len=56, tilt1=-4.7, tilt2=-5.6),
    'Futuristic':  dict(ref='Andromeda (200) / Critical Freak (185)', t20=110, f25=125, f50=82,  click=-3,  len=60, tilt1=-5.2, tilt2=-5.6),
    'Alien':       dict(ref='ein unze (185)', t20=118, f25=118, f50=80,  click=-10, len=48, tilt1=-7.0, tilt2=-9.0),
    'Biomech':     dict(ref='vs Manullu (190)', t20=145, f25=98,  f50=75,  click=-18, len=68, tilt1=-2.5, tilt2=-3.2),
    'Holzig':      dict(ref='Critical Freak - Back (185)', t20=125, f25=110, f50=68,  click=-4,  len=55, tilt1=-4.5, tilt2=-4.3),
    'Future':      dict(ref='Critical Freak (185) / vs Manullu (190)', t20=128, f25=104, f50=72, click=-8, len=62, tilt1=-3.8, tilt2=-4.5),
    'High Tech':   dict(ref='psycore 214', t20=100, f25=118, f50=88,  click=-13, len=36, tilt1=-6.5, tilt2=-9.5),
}

def kick_sweep(start, end, f5, f25, f50):
    """Startfrequenz, Pitch-Zeit T und Kurve c so wählen, dass der Verlauf
    f(t) = end * exp(ln(start/end) * (1 - t/T)^c) (Modell der KickEngine)
    die Referenzpunkte f(5 ms), f(25 ms), f(50 ms) im Log-Sinn möglichst genau trifft.
    Die Startfrequenz aus dem Entwurf ist nur Startwert (Bereich 0,6..1,6x)."""
    import numpy as np
    from scipy.optimize import minimize
    def f(t, st, T, c):
        return end * math.exp(math.log(st / end) * (max(0.0, 1 - t / T)) ** c)
    def cost(x):
        st, T, c = math.exp(x[0]), math.exp(x[1]), x[2]
        if c < 0.3 or c > 8 or T < 52: return 1e3
        return sum(w * (math.log(f(t, st, T, c)) - math.log(ft)) ** 2 for t, ft, w in ((5, f5, 0.5), (25, f25, 1.0), (50, f50, 1.0)))
    best = None
    for T0 in (60, 90, 140, 220, 350):
        for c0 in (1.0, 2.0, 3.5, 6.0):
            r = minimize(cost, [math.log(start), math.log(T0), c0], method='Nelder-Mead', options=dict(xatol=1e-4, fatol=1e-8, maxiter=4000))
            if best is None or r.fun < best.fun: best = r
    st, T, c = math.exp(best.x[0]), math.exp(best.x[1]), best.x[2]
    st = min(max(st, 0.6 * start, 120.0), 1.6 * start)
    return round(st, 1), round(T, 1), round(c, 3)

def pattern(kind, notes=None, slides=(), vel=None, len_pct=70):
    """Liefert (grid, steps). steps: [kick, kickVel, bass, bassVel, note, lenPct, slide] x 32"""
    if kind in ('KBBB', 'KxBB', 'KBxB'):
        grid, per = 0, 4
        bass_slots = {'KBBB': [1, 2, 3], 'KxBB': [2, 3], 'KBxB': [1, 3]}[kind]
    elif kind in ('TRIP', 'TRIPGAL'):
        grid, per = 1, 6
        bass_slots = {'TRIP': [2, 3, 4, 5], 'TRIPGAL': [2, 4, 5]}[kind]
    else:  # 'ROLL32', 'ROLL32B'
        grid, per = 2, 8
        bass_slots = {'ROLL32': [3, 4, 5, 6, 7], 'ROLL32B': [4, 5, 6, 7]}[kind]
    n = per * 4
    steps = []
    bi = 0
    for i in range(32):
        s = [0, 115, 0, 100, 0, len_pct, 0]
        if i < n:
            pos = i % per
            if pos == 0: s[0] = 1
            if pos in bass_slots:
                s[2] = 1
                if notes: s[4] = notes[bi % len(notes)]
                if vel: s[3] = vel[bi % len(vel)]
                if bi in slides: s[6] = 1
                bi += 1
        steps.append(s)
    return grid, steps

def make(name, theme, bpm, key, scale, kind, kick, bass, notes=None, slides=(), vel=None, len_pct=70, extra=None, octave=0):
    t = THEMES[theme]
    pc = NOTE.index(key)
    root = 24 + pc + (12 if pc < 4 else 0) + 12 * octave   # Grundton 41..62 Hz (E0..D#1)
    if hz(root) > 62: root -= 12
    sc = SCALES[scale]
    if notes:
        for nn in notes:
            assert ((nn % 12) in INTERVALS[sc]), f"{name}: Ton {nn} nicht in Skala"
    start = kick.pop('start', 420.0)
    start, T, c = kick_sweep(start, hz(root), t.get('f5', 210), t['f25'], t['f50'])
    grid, steps = pattern(kind, notes, slides, vel, len_pct)
    targets = {k: v for k, v in t.items() if k != 'ref'}
    step_ms = 60000.0 / bpm * (0.25, 1 / 6, 0.125)[grid]
    targets['len'] = round(min(targets['len'], 0.85 * step_ms), 1)
    params = {
        'kick_start_hz': start, 'kick_end_hz': round(hz(root), 3), 'kick_pitch_ms': T, 'kick_pitch_curve': c,
        'seq_root': root, 'key_root': pc, 'key_scale': sc, 'seq_grid': grid,
    }
    params.update({('kick_' + k if not k.startswith(('kick_', 'bass_', 'fit_', 'seq_', 'master')) else k): v for k, v in kick.items()})
    params.update({('bass_' + k if not k.startswith(('kick_', 'bass_', 'fit_', 'seq_', 'master')) else k): v for k, v in bass.items()})
    if extra: params.update(extra)
    return dict(name=name, theme=theme, bpm=bpm, key=f"{key} {['Moll','Phrygisch','Harm. Moll','Phryg.-Dominant','Dur','Dorisch'][sc]}",
                pattern_kind=kind, targets=targets, reference=t['ref'],
                params=params, pattern=steps)

P = []
# ---------------------------------------------------------------- Industrial (3)
P.append(make('Rostige Mantra-Maschine', 'Industrial', 200, 'F#', 'phryg', 'KBBB',
    dict(start=560, hold_ms=14, decay_ms=150, decay_curve=2.2, tail_level=0.08, tail_ms=50, click_level=-8, click_decay=1.6, click_tone=4200, drive=11),
    dict(saw=0.5, square=0.4, sub=0.35, wt_level=0.7, wt_table=5, wt_pos=0.55, wt_env=0.45, cutoff=700, reso=0.5, fenv_amt=2.6,
         fenv_dec=60, att=0.3, dec=80, sus=0.3, rel=5, drive=13, glide=15),
    notes=[0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 12, 0], len_pct=75))
P.append(make('Stahlkathedrale', 'Industrial', 196, 'G', 'phrygdom', 'KBBB',
    dict(start=520, hold_ms=12, decay_ms=150, decay_curve=2.2, tail_level=0.08, tail_ms=50, click_level=-6, click_decay=1.2, click_tone=5500, drive=11),
    dict(saw=0.3, square=0.6, pw=0.3, sub=0.35, wt_level=0.8, wt_table=3, wt_pos=0.6, wt_env=0.5, cutoff=750, reso=0.55, fenv_amt=3.0,
         fenv_dec=50, att=0.2, dec=70, sus=0.25, rel=4, drive=14),
    notes=[0, 0, 0, 0, 0, 0, 1, 0, 0, 4, 0, 1], len_pct=75))
P.append(make('Kolben im Kosmos', 'Industrial', 200, 'E', 'phryg', 'KBxB',
    dict(start=520, hold_ms=16, decay_ms=160, decay_curve=2.0, tail_level=0.1, tail_ms=55, click_level=-7, click_decay=2.0, click_tone=3600, drive=10),
    dict(saw=0.6, square=0.2, sub=0.4, wt_level=0.6, wt_table=4, wt_pos=0.4, wt_env=0.6, cutoff=650, reso=0.45, fenv_amt=2.8,
         fenv_dec=65, att=0.3, dec=90, sus=0.35, rel=6, drive=12, pick_amt=0.3, pick_pos=0.18),
    notes=[0, 0, 0, 0, 0, 12, 0, 1], len_pct=80))
# ---------------------------------------------------------------- Psy-Fi Dark (4)
P.append(make('Nachtschatten-Nebula', 'Psy-Fi Dark', 180, 'D', 'phryg', 'TRIP',
    dict(start=420, hold_ms=18, decay_ms=130, decay_curve=2.0, tail_level=0.08, tail_ms=40, click_level=-14, click_decay=3.0, click_tone=2600, drive=6),
    dict(saw=0.4, sub=0.6, wt_level=0.6, wt_table=2, wt_pos=0.5, wt_env=0.35, cutoff=520, reso=0.35, fenv_amt=2.0,
         fenv_dec=70, att=0.6, dec=70, sus=0.4, rel=8, drive=4, width=0.3, width_hz=500),
    notes=[0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 3, 0, 0, 0, 0, 0], len_pct=85))
P.append(make('Mondfresser', 'Psy-Fi Dark', 182, 'C#', 'moll', 'TRIPGAL',
    dict(start=400, hold_ms=20, decay_ms=135, decay_curve=1.8, tail_level=0.1, tail_ms=45, click_level=-13, click_decay=3.5, click_tone=2200, drive=5),
    dict(saw=0.5, sub=0.55, wt_level=0.5, wt_table=1, wt_pos=0.3, wt_env=0.3, cutoff=480, reso=0.4, fenv_amt=2.2,
         fenv_dec=80, att=0.5, dec=80, sus=0.45, rel=8, drive=5, glide=40),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 3, 0], slides=(8, 10), len_pct=90))
P.append(make('Schwarzlicht-Sutra', 'Psy-Fi Dark', 180, 'A', 'phrygdom', 'TRIP',
    dict(start=440, hold_ms=16, decay_ms=125, decay_curve=2.0, tail_level=0.08, tail_ms=35, click_level=-15, click_decay=2.5, click_tone=3000, drive=6),
    dict(saw=0.5, square=0.2, sub=0.5, wt_level=0.6, wt_table=0, wt_pos=0.6, wt_env=0.25, cutoff=700, reso=0.3, fenv_amt=2.0,
         fenv_dec=75, att=0.8, dec=75, sus=0.4, rel=10, drive=3, width=0.35, width_hz=600, vel_amp=0.5),
    notes=[0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 4, 0, 1, 0], vel=[112, 84, 98, 90], len_pct=85))
P.append(make('Spektrale Sümpfe', 'Psy-Fi Dark', 184, 'F', 'moll', 'TRIP',
    dict(start=410, hold_ms=22, decay_ms=140, decay_curve=1.7, tail_level=0.1, tail_ms=40, click_level=-12, click_decay=3.0, click_tone=2400, drive=6),
    dict(saw=0.45, sub=0.55, wt_level=0.65, wt_table=1, wt_pos=0.55, wt_env=0.5, cutoff=500, reso=0.45, fenv_amt=2.4,
         fenv_dec=65, att=0.5, dec=70, sus=0.35, rel=7, drive=5),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0], len_pct=85))
# ---------------------------------------------------------------- Futuristic (3)
P.append(make('Chrom-Prophet', 'Futuristic', 200, 'G#', 'moll', 'KBBB',
    dict(start=520, hold_ms=14, decay_ms=150, decay_curve=2.2, tail_level=0.08, tail_ms=50, click_level=-9, click_decay=1.8, click_tone=5000, drive=8),
    dict(saw=0.7, sub=0.4, wt_level=0.7, wt_table=0, wt_pos=0.35, wt_env=0.4, cutoff=760, reso=0.4, fenv_amt=2.4,
         fenv_dec=60, att=0.3, dec=85, sus=0.35, rel=5, drive=7),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 7], len_pct=75))
P.append(make('Quantensamen', 'Futuristic', 198, 'A#', 'dor', 'KBBB',
    dict(start=500, hold_ms=16, decay_ms=155, decay_curve=2.0, tail_level=0.1, tail_ms=55, click_level=-8, click_decay=2.0, click_tone=4500, drive=8),
    dict(saw=0.4, square=0.3, sub=0.4, wt_level=0.8, wt_table=3, wt_pos=0.3, wt_env=0.6, cutoff=700, reso=0.45, fenv_amt=2.6,
         fenv_dec=55, att=0.3, dec=80, sus=0.3, rel=5, drive=8),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3], len_pct=75))
P.append(make('Neon-Nirvana', 'Futuristic', 192, 'F#', 'moll', 'KxBB',
    dict(start=480, hold_ms=18, decay_ms=165, decay_curve=2.0, tail_level=0.1, tail_ms=60, click_level=-8, click_decay=2.2, click_tone=4800, drive=7),
    dict(saw=0.7, sub=0.45, wt_level=0.5, wt_table=0, wt_pos=0.5, wt_env=0.3, cutoff=800, reso=0.35, fenv_amt=2.0,
         fenv_dec=70, att=0.4, dec=95, sus=0.4, rel=6, drive=6, width=0.25, width_hz=500),
    notes=[0, 0, 0, 0, 0, 0, 7, 0], len_pct=80))
# ---------------------------------------------------------------- Alien (3)
P.append(make('Xenomantra', 'Alien', 185, 'E', 'phrygdom', 'TRIP',
    dict(start=460, hold_ms=20, decay_ms=160, decay_curve=1.8, tail_level=0.1, tail_ms=55, click_level=-14, click_decay=3.0, click_tone=2800, drive=6),
    dict(saw=0.3, sub=0.5, wt_level=0.9, wt_table=4, wt_pos=0.25, wt_env=0.5, cutoff=480, reso=0.5, fenv_amt=2.8,
         fenv_dec=55, att=0.4, dec=55, sus=0.3, rel=5, drive=6, glide=50),
    notes=[0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 4, 0, 5, 0], slides=(6, 12), len_pct=75))
P.append(make('Sternsaat-Signal', 'Alien', 186, 'G', 'phryg', 'TRIPGAL',
    dict(start=480, hold_ms=18, decay_ms=165, decay_curve=1.9, tail_level=0.1, tail_ms=55, click_level=-13, click_decay=2.8, click_tone=3200, drive=6),
    dict(saw=0.2, square=0.3, sub=0.5, wt_level=0.9, wt_table=4, wt_pos=0.6, wt_env=-0.5, cutoff=520, reso=0.55, fenv_amt=3.0,
         fenv_dec=50, att=0.3, dec=55, sus=0.25, rel=5, drive=7),
    notes=[0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 3], len_pct=75))
P.append(make('Tentakel-Telepathie', 'Alien', 188, 'D#', 'harm', 'TRIP',
    dict(start=450, hold_ms=22, decay_ms=170, decay_curve=1.7, tail_level=0.12, tail_ms=60, click_level=-15, click_decay=3.2, click_tone=2600, drive=5),
    dict(saw=0.4, sub=0.5, wt_level=0.8, wt_table=5, wt_pos=0.35, wt_env=0.7, cutoff=500, reso=0.45, fenv_amt=2.5,
         fenv_dec=60, att=0.4, dec=60, sus=0.3, rel=6, drive=6, glide=60, width=0.3, width_hz=450),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 0, 0, 0, 7, 0], slides=(10, 14), len_pct=80))
# ---------------------------------------------------------------- Biomech (3)
P.append(make('Biomechanischer Lotus', 'Biomech', 190, 'H', 'phryg', 'KxBB',
    dict(start=420, hold_ms=30, decay_ms=200, decay_curve=1.6, tail_level=0.14, tail_ms=80, click_level=-24, click_decay=4.0, click_tone=1800, drive=7),
    dict(saw=0.6, sub=0.35, wt_level=0.7, wt_table=5, wt_pos=0.65, wt_env=0.4, cutoff=950, reso=0.5, fenv_amt=2.2,
         fenv_dec=90, att=0.5, dec=120, sus=0.45, rel=8, drive=12, pick_amt=0.35, pick_pos=0.15, damper=0.2),
    notes=[0, 0, 0, 0, 0, 1, 0, 0], len_pct=95))
P.append(make('Wirbelknochen-Orgel', 'Biomech', 188, 'C', 'phrygdom', 'KxBB',
    dict(start=400, hold_ms=32, decay_ms=210, decay_curve=1.5, tail_level=0.15, tail_ms=85, click_level=-22, click_decay=4.5, click_tone=1600, drive=8),
    dict(saw=0.5, square=0.3, sub=0.35, wt_level=0.6, wt_table=1, wt_pos=0.8, wt_env=0.3, cutoff=1000, reso=0.55, fenv_amt=2.0,
         fenv_dec=100, att=0.6, dec=130, sus=0.5, rel=9, drive=13),
    notes=[0, 0, 0, 0, 0, 0, 1, 4], len_pct=80))
P.append(make('Nekro-Kathedrale', 'Biomech', 190, 'A', 'moll', 'KBxB',
    dict(start=440, hold_ms=28, decay_ms=195, decay_curve=1.6, tail_level=0.13, tail_ms=75, click_level=-20, click_decay=4.0, click_tone=2000, drive=9),
    dict(saw=0.7, sub=0.3, wt_level=0.6, wt_table=3, wt_pos=0.5, wt_env=0.4, cutoff=900, reso=0.5, fenv_amt=2.4,
         fenv_dec=85, att=0.5, dec=115, sus=0.45, rel=8, drive=14, pick_amt=0.25, pick_pos=0.22),
    notes=[0, 0, 0, 0, 0, 0, 0, 3], len_pct=95))
# ---------------------------------------------------------------- Holzig (3)
P.append(make('Wurzelgeist', 'Holzig', 185, 'G', 'phryg', 'KxBB',
    dict(start=440, hold_ms=24, decay_ms=170, decay_curve=1.8, tail_level=0.12, tail_ms=60, click_level=-10, click_decay=2.2, click_tone=1700, drive=6),
    dict(saw=0.3, sub=0.55, wt_level=0.8, wt_table=1, wt_pos=0.35, wt_env=0.35, cutoff=620, reso=0.45, fenv_amt=2.2,
         fenv_dec=70, att=0.3, dec=75, sus=0.35, rel=6, drive=6, pick_amt=0.5, pick_pos=0.14, damper=0.4),
    notes=[0, 0, 0, 0, 0, 0, 1, 0], len_pct=70))
P.append(make('Rindenrune', 'Holzig', 183, 'F#', 'moll', 'KBBB',
    dict(start=430, hold_ms=22, decay_ms=165, decay_curve=1.9, tail_level=0.1, tail_ms=55, click_level=-9, click_decay=2.5, click_tone=1500, drive=6),
    dict(saw=0.4, sub=0.5, wt_level=0.7, wt_table=1, wt_pos=0.55, wt_env=0.25, cutoff=600, reso=0.4, fenv_amt=2.0,
         fenv_dec=75, att=0.3, dec=70, sus=0.35, rel=6, drive=5, pick_amt=0.6, pick_pos=0.12, damper=0.55),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 3, 0], len_pct=70))
P.append(make('Moosorakel', 'Holzig', 185, 'A', 'dor', 'KxBB',
    dict(start=420, hold_ms=26, decay_ms=175, decay_curve=1.7, tail_level=0.12, tail_ms=60, click_level=-11, click_decay=2.0, click_tone=1900, drive=5),
    dict(saw=0.5, sub=0.5, wt_level=0.6, wt_table=1, wt_pos=0.2, wt_env=0.5, cutoff=580, reso=0.5, fenv_amt=2.4,
         fenv_dec=65, att=0.3, dec=75, sus=0.3, rel=6, drive=6, pick_amt=0.45, pick_pos=0.2, damper=0.35),
    notes=[0, 0, 0, 0, 0, 2, 0, 0], len_pct=70))
# ---------------------------------------------------------------- Future (3)
P.append(make('Morgenkristall', 'Future', 185, 'G#', 'dur', 'KxBB',
    dict(start=440, hold_ms=24, decay_ms=175, decay_curve=1.8, tail_level=0.12, tail_ms=60, click_level=-14, click_decay=2.5, click_tone=4200, drive=5),
    dict(saw=0.8, sub=0.4, wt_level=0.5, wt_table=0, wt_pos=0.2, wt_env=0.3, cutoff=850, reso=0.3, fenv_amt=1.8,
         fenv_dec=80, att=0.4, dec=95, sus=0.4, rel=7, drive=4, width=0.3, width_hz=500),
    notes=[0, 0, 0, 0, 0, 4, 0, 0], len_pct=80))
P.append(make('Lichtgitter', 'Future', 188, 'H', 'moll', 'KBBB',
    dict(start=450, hold_ms=22, decay_ms=170, decay_curve=1.9, tail_level=0.1, tail_ms=55, click_level=-13, click_decay=2.2, click_tone=5000, drive=6),
    dict(saw=0.6, square=0.2, sub=0.4, wt_level=0.6, wt_table=3, wt_pos=0.15, wt_env=0.4, cutoff=820, reso=0.35, fenv_amt=2.0,
         fenv_dec=70, att=0.3, dec=85, sus=0.35, rel=6, drive=5),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 5], len_pct=75))
P.append(make('Zeitlinien-Zauber', 'Future', 190, 'E', 'moll', 'KxBB',
    dict(start=430, hold_ms=26, decay_ms=180, decay_curve=1.8, tail_level=0.12, tail_ms=65, click_level=-12, click_decay=2.5, click_tone=4400, drive=5),
    dict(saw=0.6, sub=0.45, wt_level=0.7, wt_table=0, wt_pos=0.6, wt_env=0.35, cutoff=780, reso=0.35, fenv_amt=2.2,
         fenv_dec=75, att=0.4, dec=90, sus=0.4, rel=7, drive=5, glide=40),
    notes=[0, 0, 0, 0, 0, 0, 7, 0], slides=(6,), len_pct=80))
# ---------------------------------------------------------------- High Tech (3)
P.append(make('Hyperlotus 214', 'High Tech', 214, 'G', 'phryg', 'TRIP',
    dict(start=520, hold_ms=12, decay_ms=140, decay_curve=2.2, tail_level=0.06, tail_ms=40, click_level=-12, click_decay=1.5, click_tone=5000, drive=8),
    dict(saw=0.6, sub=0.45, wt_level=0.6, wt_table=3, wt_pos=0.4, wt_env=0.5, cutoff=600, reso=0.45, fenv_amt=2.4,
         fenv_dec=35, att=0.2, dec=40, sus=0.2, rel=3, drive=7),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 12, 0], len_pct=80))
P.append(make('Synapsen-Sturm', 'High Tech', 210, 'F#', 'phrygdom', 'ROLL32B',
    dict(start=540, hold_ms=10, decay_ms=135, decay_curve=2.4, tail_level=0.05, tail_ms=35, click_level=-11, click_decay=1.4, click_tone=5500, drive=9),
    dict(saw=0.4, square=0.3, sub=0.45, wt_level=0.7, wt_table=4, wt_pos=0.3, wt_env=0.6, cutoff=580, reso=0.5, fenv_amt=2.6,
         fenv_dec=30, att=0.2, dec=35, sus=0.2, rel=3, drive=8),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 4, 0, 0, 0, 0], len_pct=80))
P.append(make('Fraktal-Feuer', 'High Tech', 205, 'A', 'harm', 'TRIPGAL',
    dict(start=500, hold_ms=12, decay_ms=145, decay_curve=2.2, tail_level=0.07, tail_ms=40, click_level=-13, click_decay=1.6, click_tone=4600, drive=8),
    dict(saw=0.5, sub=0.45, wt_level=0.7, wt_table=5, wt_pos=0.45, wt_env=0.55, cutoff=620, reso=0.45, fenv_amt=2.4,
         fenv_dec=35, att=0.2, dec=40, sus=0.2, rel=3, drive=8, glide=20),
    notes=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 7, 0], len_pct=80))

assert len(P) == 25, len(P)
json.dump(P, open('presets_design.json', 'w'), indent=1, ensure_ascii=False)
print('25 Presets entworfen')
for p in P: print(f"{p['theme']:12s} {p['name']:26s} {p['bpm']} BPM {p['key']:22s} {p['pattern_kind']:8s} pitch {p['params']['kick_pitch_ms']} ms c {p['params']['kick_pitch_curve']}")
