"""Referenz-Analyse für Samsi Bassline: misst Kick & Bass aus fertigen Tracks.

Verfahren (alles messbar, keine Ohr-Schätzung):
  1. Tempo: Autokorrelation der Tiefband-Onset-Hüllkurve (Suchbereich 160-230 BPM).
  2. Beat-Raster: Phase der Kick über gefaltete Onset-Energie, je Schlag feinjustiert (±4 ms).
  3. "Kick-Bass-Schläge": Schläge mit klarer Kick und Bassenergie zwischen den Kicks.
  4. Schlag-synchroner Mittelwert (Median-Hüllkurve) -> Kick-Form, Bass-Raster, Notenlänge.
  5. Tonhöhe: Kick-Grundton aus dem Kick-Tail vor dem ersten Bass, Bass-Töne je Sechzehntel (NSDF).
"""
import json, sys, os
import numpy as np
from scipy import signal

def butter(x, sr, f, kind, order=4):
    sos = signal.butter(order, f, btype=kind, fs=sr, output='sos')
    return signal.sosfiltfilt(sos, x)

def env_rms(x, sr, win_ms):
    n = max(1, int(sr * win_ms / 1000))
    k = np.ones(n) / n
    return np.sqrt(np.convolve(x * x, k, mode='same') + 1e-20)

def nsdf_pitch(x, sr, fmin, fmax):
    x = x - x.mean()
    n = len(x)
    tmin, tmax = int(sr / fmax), int(sr / fmin)
    if n < int(tmax * 1.6) + 4: return 0.0, 0.0
    W = n - tmax
    best = (0, 0.0)
    vals = np.zeros(tmax + 2)
    a = x[:W]
    for tau in range(max(2, tmin - 1), min(tmax + 2, n - W + 1)):
        b = x[tau:tau + W]
        m = np.dot(a, a) + np.dot(b, b)
        vals[tau] = 2 * np.dot(a, b) / m if m > 0 else 0
    g = vals[tmin:tmax + 1].max()
    if g < 0.5: return 0.0, g
    for tau in range(tmin, tmax + 1):
        v = vals[tau]
        if v >= 0.9 * g and v >= vals[tau - 1] and v >= vals[tau + 1]:
            ym, y0, yp = vals[tau - 1], v, vals[tau + 1]
            d = ym - 2 * y0 + yp
            sh = 0.5 * (ym - yp) / d if abs(d) > 1e-12 else 0
            return sr / (tau + max(-.5, min(.5, sh))), v
    return 0.0, g

NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
def hz2note(hz):
    m = 69 + 12 * np.log2(hz / 440.0)
    r = int(round(m))
    return f"{NOTE[r % 12]}{r // 12 - 2}", (m - r) * 100, r

def analyze(path, bpm_hint=None):
    name = os.path.basename(path)[:-4]
    sr = int(open(path[:-4] + '.sr').read())
    x = np.load(path).astype(np.float64)

    # --- Analyse auf 11025 Hz für Tiefband, volle Rate für Click ---
    ds = sr // 11025 if sr % 11025 == 0 else 4
    lp = butter(x, sr, 300, 'low')
    lo = signal.resample_poly(lp, 1, ds)
    lsr = sr / ds

    # --- Tempo (grob) ---
    e2 = env_rms(lo, lsr, 2)
    onset = np.maximum(0, np.diff(env_rms(lo, lsr, 4), prepend=0))
    hop = int(lsr * 0.002)
    oe = onset[: len(onset) // hop * hop].reshape(-1, hop).sum(1)
    osr = lsr / hop
    oe = oe - oe.mean()
    n = min(int(osr * 60), len(oe) // 2)
    s0 = min(int(osr * 60), max(0, len(oe) - n - 1))
    seg = oe[s0:s0 + n]
    ac = np.correlate(seg, seg, 'full')[n - 1:]
    best_bpm, best_v = 0, -1e9
    for bpm10 in range(1600, 2300):
        b = bpm10 / 10
        lag = 60 / b * osr
        i = int(lag)
        v = ac[i] * (1 - (lag - i)) + ac[i + 1] * (lag - i)
        if v > best_v: best_v, best_bpm = v, b
    bpm0 = bpm_hint if bpm_hint and abs(bpm_hint - best_bpm) < 4 else best_bpm

    # laute Abschnitte (Kick+Bass laufen) über die Hüllkurve in 1-s-Blöcken
    blk = int(lsr)
    blk_rms = np.array([np.sqrt(np.mean(lo[i:i + blk] ** 2)) for i in range(0, len(lo) - blk, blk)])
    loud_blocks = blk_rms > np.percentile(blk_rms, 45)

    def fold(bpm_):
        per = 60 / bpm_ * lsr
        F = np.zeros(int(per))
        cnt = 0
        k = 0
        while True:
            s_ = int(k * per)
            if s_ + int(per) >= len(e2): break
            if loud_blocks[min(len(loud_blocks) - 1, s_ // blk)]:
                F += e2[s_:s_ + int(per)]; cnt += 1
            k += 1
        return F / max(1, cnt), per
    # Feinabgleich des Tempos: maximaler Kontrast des gefalteten Schlags
    best = None
    for d in np.linspace(-0.3, 0.3, 25):
        F, per = fold(bpm0 + d)
        c = F.max() / (F.mean() + 1e-12)
        if best is None or c > best[0]: best = (c, bpm0 + d, F, per)
    _, bpm, F, period = best
    # Kick-Einsatz: stärkster Anstieg des gefalteten Tiefbands
    Fs = np.convolve(F, np.ones(int(0.004 * lsr)) / int(0.004 * lsr), 'same')
    dF = np.diff(Fs, append=Fs[0])
    ph = int(np.argmax(dF))
    # vom steilsten Anstieg zurück bis zur Schwelle min + 10 % -> tatsächlicher Einsatz
    lo_v, hi_v = Fs.min(), Fs.max()
    thr = lo_v + 0.1 * (hi_v - lo_v)
    j = ph
    for _ in range(int(0.06 * lsr)):
        if Fs[j % len(Fs)] <= thr: break
        j -= 1
    ph = j % len(Fs)
    sm = env_rms(lo, lsr, 1)

    beats = []
    k = 0
    while True:
        c = int(ph + k * period)
        k += 1
        w = int(0.004 * lsr)
        if c + int(period) + w >= len(lo): break
        if c - w < int(0.05 * lsr): continue
        if not loud_blocks[min(len(loud_blocks) - 1, c // blk)]: continue
        # Einsatz je Schlag: erstes Überschreiten von 15 % des lokalen Kick-Maximums
        win = sm[c - w: c + int(0.03 * lsr)]
        base = sm[c - int(0.012 * lsr): c - int(0.002 * lsr)].min()
        top = win.max()
        idx = np.where(win > base + 0.15 * (top - base))[0]
        beats.append(c - w + int(idx[0]) if len(idx) else c)
    beats = np.array(beats)

    kick_strength, pre = [], []
    for c in beats:
        a = sm[c + int(0.003 * lsr): c + int(0.02 * lsr)].mean()
        b = sm[c - int(0.012 * lsr): c - int(0.002 * lsr)].mean()
        kick_strength.append(a / (b + 1e-9)); pre.append(a)
    kick_strength = np.array(kick_strength); pre = np.array(pre)
    good = (kick_strength > 1.8) & (pre > np.percentile(pre, 30))
    sel = beats[good]
    if len(sel) < 32:
        sel = beats[np.argsort(-kick_strength)[:max(32, len(beats) // 3)]]
        sel.sort()

    # --- Schlag-synchroner Mittelwert (Tiefband) ---
    L = int(period)
    pre_n = int(0.005 * lsr)
    segs = np.stack([lo[c - pre_n: c - pre_n + L + pre_n] for c in sel])
    # Polarität angleichen (manche Kicks starten negativ)
    ref = np.median(segs, axis=0)
    flips = np.ones(len(segs))
    for i in range(len(segs)):
        if np.dot(segs[i, :pre_n + int(0.03 * lsr)], ref[:pre_n + int(0.03 * lsr)]) < 0:
            segs[i] *= -1; flips[i] = -1
    avg = segs.mean(axis=0)
    envs = np.stack([env_rms(s, lsr, 2.5) for s in segs])
    med_env = np.median(envs, axis=0)
    t_ms = (np.arange(len(avg)) - pre_n) / lsr * 1000

    # --- Kick: Pitch-Verlauf (Nulldurchgänge im Mittelwert) ---
    k0 = pre_n
    # Pitch-Verlauf aus einem breiteren Band (bis 2 kHz), synchron gemittelt
    lo2k = butter(x, sr, 700, 'low')
    wide = []
    for i, c in enumerate(sel[:300]):
        cf_ = int(c * ds)
        w_ = lo2k[cf_ - int(pre_n * ds): cf_ + int(0.1 * sr)]
        if len(w_) == int(pre_n * ds) + int(0.1 * sr): wide.append(w_ * flips[i])
    wavg = np.mean(wide, axis=0)
    np.save(path[:-4] + '_kickwide.npy', wavg)
    zc = np.where(np.diff(np.signbit(wavg[int(pre_n * ds):])))[0]
    zc = zc[zc < int(0.08 * sr)] * (lsr / sr)
    zt = zc / lsr * 1000
    inst = []
    for i in range(1, len(zt)):
        half = (zt[i] - zt[i - 1]) / 1000
        if half > 0: inst.append(((zt[i] + zt[i - 1]) / 2, 0.5 / half))
    inst = np.array(inst) if inst else np.zeros((0, 2))
    if len(inst): inst = inst[(inst[:, 1] > 30) & (inst[:, 1] < 700)]

    # Bass-Einsätze: Anstiege der Median-Hüllkurve nach dem Kick, auf 1/16- oder 1/12-Raster bezogen
    step = period / 4
    env_db = 20 * np.log10(med_env + 1e-12)
    peak_db = env_db[pre_n:pre_n + int(0.08 * lsr)].max()
    # geglättete Hüllkurve (Welligkeit der tiefen Schwingungen entfernen)
    sme = np.median(np.stack([env_rms(sg, lsr, 12) for sg in segs]), axis=0)
    d = np.diff(sme, append=sme[-1])
    min_gap = int(period / 8)
    cand = []
    # Kick-Ende: erstes lokales Minimum nach dem Maximum, das mindestens 12 dB unter dem Maximum liegt
    kpk = pre_n + int(np.argmax(sme[pre_n:pre_n + int(0.08 * lsr)]))
    kick_end = None
    for i in range(kpk + int(0.01 * lsr), pre_n + int(period * 0.9)):
        if 20 * np.log10(sme[i] + 1e-12) < 20 * np.log10(sme[kpk]) - 12 and sme[i] <= sme[i - 1] and sme[i] <= sme[i + 1]:
            kick_end = i; break
    if kick_end is None: kick_end = pre_n + int(period * 0.25)
    i0 = kick_end
    for i in range(i0, pre_n + int(period * 0.97)):
        lo_i, hi_i = max(0, i - min_gap // 2), min(len(d), i + min_gap // 2)
        if d[i] == d[lo_i:hi_i].max() and d[i] > 0.15 * d[pre_n:pre_n + int(0.02 * lsr)].max():
            cand.append(i - pre_n)
    # zu dicht beieinander liegende zusammenfassen, dann vom steilsten Anstieg zurück zum Einsatz (lokales Min + 10 %)
    slot_on = []
    for c_ in cand:
        if not slot_on or c_ - slot_on[-1] > min_gap: slot_on.append(c_)
    refined = []
    for c_ in slot_on:
        i = pre_n + c_
        j = i
        while j > i - min_gap and sme[j - 1] < sme[j]: j -= 1
        lo_v = sme[j]
        k_ = j
        while k_ < i and sme[k_] < lo_v + 0.1 * (sme[i] - lo_v): k_ += 1
        refined.append(k_ - pre_n)
    slot_on = refined
    if not slot_on: slot_on = [int(step), int(2 * step), int(3 * step)]
    first_bass = slot_on[0]
    # Kick-Länge: -20 dB unter Maximum oder erster Bass
    sm_db = 20 * np.log10(sme + 1e-12)
    kpk_i = int(np.argmax(sme[pre_n:pre_n + int(0.08 * lsr)]))
    kick_peak_ms = kpk_i / lsr * 1000
    kick_env = sm_db[pre_n + kpk_i:pre_n + first_bass]
    def t_at(db):
        idx = np.where(kick_env < sm_db[pre_n + kpk_i] - db)[0]
        return (kpk_i + idx[0]) / lsr * 1000 if len(idx) else first_bass / lsr * 1000
    kick_t6, kick_t12, kick_t20 = t_at(6), t_at(12), t_at(20)
    kick_level_at_bass = sm_db[pre_n + first_bass] - sm_db[pre_n + kpk_i]

    # Kick-Grundton: Median über Schläge im Bereich 35 ms .. erster Bass - 5 ms
    kf = []
    for s in segs[: 400]:
        a = pre_n + int(0.055 * lsr); b = min(pre_n + first_bass, kick_end) - int(0.004 * lsr)
        if b - a > lsr / 25:
            f, cf = nsdf_pitch(s[a:b], lsr, 35, 120)
            if f > 0 and cf > 0.7: kf.append(f)
    kick_f0 = float(np.median(kf)) if kf else 0.0
    # Ersatz: Frequenz der letzten Halbwellen vor dem Kick-Ende (aus dem breitbandigen Mittelwert)
    zc2 = np.where(np.diff(np.signbit(wavg[int(pre_n * ds):int(pre_n * ds) + int((kick_end - pre_n) * ds)])))[0]
    tail_f = []
    for i in range(1, len(zc2)):
        t_mid = (zc2[i] + zc2[i - 1]) / 2 / sr * 1000
        f_ = 0.5 / ((zc2[i] - zc2[i - 1]) / sr)
        if t_mid > 45 and 30 < f_ < 120: tail_f.append(f_)
    kick_tail_hz = float(np.median(tail_f[-6:])) if tail_f else 0.0
    if kick_f0 == 0.0: kick_f0 = kick_tail_hz

    # Bass-Töne je Sechzehntel (über alle ausgewählten Schläge)
    notes, lens, attacks = [], [], []
    for s_i, s in enumerate(segs):
        e_s = envs[s_i]
        for k, on in enumerate(slot_on):
            a = pre_n + on
            b = pre_n + (slot_on[k + 1] if k + 1 < len(slot_on) else int(period))
            seg = s[a + int(0.004 * lsr): b - int(0.002 * lsr)]
            if len(seg) > lsr / 40:
                f, cf = nsdf_pitch(seg, lsr, 36, 160)
                if f > 0 and cf > 0.7: notes.append(f)
            es = e_s[a:b]
            if len(es) > 4 and es.max() > 0:
                pk = es.max()
                above = np.where(es > pk * 10 ** (-12 / 20))[0]
                lens.append((above[-1] - above[0]) / lsr * 1000 if len(above) else 0)
                i10 = np.argmax(es > pk * 0.1); i90 = np.argmax(es > pk * 0.9)
                attacks.append(max(0, i90 - i10) / lsr * 1000)
    notes = np.array(notes)
    pcs = np.round(69 + 12 * np.log2(notes / 440)).astype(int) % 12 if len(notes) else np.array([], int)
    hist = np.bincount(pcs, minlength=12) / max(1, len(pcs))
    bass_root_pc = int(np.argmax(hist))
    bass_root_hz = float(np.median(notes[pcs == bass_root_pc])) if len(notes) else 0.0

    # Ducking/Überlappung: Bass-Hüllkurve im Kickbereich vs. Bass-Slots
    slot_lvls = [med_env[pre_n + o + int(0.01 * lsr)] for o in slot_on]
    # Pattern: Anteil der Schläge, in denen jeder Slot klingt
    occ = []
    for k, on in enumerate(slot_on):
        lvl = envs[:, pre_n + on + int(0.01 * lsr)]
        occ.append(float(np.mean(lvl > med_env[pre_n + slot_on[0] + int(0.01 * lsr)] * 0.25)))

    # --- Spektren (volle Bandbreite) Kick-Bereich vs. Bass-Bereich ---
    full_segs = []
    for c in sel[:300]:
        cf_ = int(c * ds)
        full_segs.append(x[cf_ - int(pre_n * ds): cf_ + int(L * ds)])
    full_segs = np.stack(full_segs)
    def band_db(seg_slice, lo_hz, hi_hz):
        X = np.abs(np.fft.rfft(full_segs[:, seg_slice] * np.hanning(seg_slice.stop - seg_slice.start), axis=1)) ** 2
        fr = np.fft.rfftfreq(seg_slice.stop - seg_slice.start, 1 / sr)
        m = (fr >= lo_hz) & (fr < hi_hz)
        return 10 * np.log10(X[:, m].sum(1).mean() + 1e-20)
    pn = int(pre_n * ds)
    ks = slice(pn, pn + int(first_bass * ds))
    bs = slice(pn + int(first_bass * ds), pn + int(L * ds) - 1)
    bands = [(20, 60), (60, 120), (120, 250), (250, 1000), (1000, 5000), (5000, 16000)]
    kick_bands = [band_db(ks, a, b) for a, b in bands]
    bass_bands = [band_db(bs, a, b) for a, b in bands]
    # Bass-Helligkeit: Schwerpunkt 60..3000 Hz im Bassbereich
    X = np.abs(np.fft.rfft(full_segs[:, bs] * np.hanning(bs.stop - bs.start), axis=1)) ** 2
    fr = np.fft.rfftfreq(bs.stop - bs.start, 1 / sr)
    m = (fr > 60) & (fr < 3000)
    centroid = float((X[:, m].mean(0) * fr[m]).sum() / X[:, m].mean(0).sum())
    # Click: Energie > 2 kHz in ersten 6 ms relativ zur Kick-Tiefband-Energie
    cs = slice(pn, pn + int(0.006 * sr))
    click_db = band_db(cs, 2000, 12000) - band_db(cs, 20, 300)

    res = dict(
        name=name, sr=sr, bpm=bpm, bpm_detected=best_bpm, beats_total=int(len(beats)), beats_used=int(len(sel)),
        step_ms=step / lsr * 1000,
        kick=dict(
            f0_hz=kick_f0, f0_note=hz2note(kick_f0)[0] if kick_f0 else '-', f0_cents=hz2note(kick_f0)[1] if kick_f0 else 0,
            sweep=[(round(a, 1), round(b, 1)) for a, b in inst[:12]],
            peak_ms=kick_peak_ms, t6_ms=kick_t6, t12_ms=kick_t12, t20_ms=kick_t20, end_ms=(kick_end - pre_n) / lsr * 1000,
            level_at_first_bass_db=float(kick_level_at_bass), click_rel_db=float(click_db),
            bands_db=[round(v, 1) for v in kick_bands]),
        bass=dict(
            onsets_ms=[o / lsr * 1000 for o in slot_on],
            onset_grid=[round(o / period * 16, 2) for o in slot_on],
            onset_steps16=[round(o / period * 4, 2) for o in slot_on],
            slot_occupancy=occ,
            note_len_ms_median=float(np.median(lens)) if lens else 0, note_len_pct=float(np.median(lens)) / (step / lsr * 1000) * 100 if lens else 0,
            attack_ms_median=float(np.median(attacks)) if attacks else 0,
            root_pc=NOTE[bass_root_pc], root_hz=bass_root_hz, pc_hist=[round(v, 3) for v in hist],
            notes_measured=int(len(notes)), centroid_hz=centroid,
            slot_levels_db=[float(20 * np.log10(v + 1e-12) - peak_db) for v in slot_lvls],
            bands_db=[round(v, 1) for v in bass_bands]),
    )
    return res, (t_ms, avg, med_env)

if __name__ == '__main__':
    out = {}
    for p in sys.argv[1:]:
        hint = None
        import re
        m = re.findall(r'(\d{3})', os.path.basename(p))
        for v in m:
            if 150 <= int(v) <= 230: hint = float(v); break
        r, curves = analyze(p, hint)
        out[r['name']] = r
        np.save(p[:-4] + '_avgbeat.npy', np.vstack(curves))
        print(json.dumps(r, indent=1, default=float)[:4000])
    json.dump(out, open('reference_metrics.json', 'w'), indent=1, default=float)
