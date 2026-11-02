"""Messung der Plugin-Renderings an getrennten Spuren (Kick / Bass) mit denselben Definitionen wie refana."""
import numpy as np
from scipy.io import wavfile
from refana import butter, env_rms, nsdf_pitch, hz2note

def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    return sr, (x.mean(1) if x.ndim == 2 else x)

def measure_stems(base, bpm, skip_bars=2):
    sr, k = load(base + '_kick.wav')
    _, b = load(base + '_bass.wav')
    beat = 60.0 / bpm * sr
    nbeats = int(len(k) / beat) - 1
    first = skip_bars * 4
    pre = int(0.005 * sr)

    # --- Kick: Mittel über Schläge ---
    lp300 = butter(k, sr, 300, 'low')
    lp700 = butter(k, sr, 700, 'low')
    segs, wide = [], []
    for i in range(first, nbeats):
        c = int(round(i * beat))
        segs.append(lp300[c - pre: c + int(beat)])
        wide.append(lp700[c - pre: c + int(0.1 * sr)])
    segs = np.array(segs); wavg = np.mean(wide, 0)
    env = np.median([env_rms(s, sr, 12) for s in segs], 0)
    db = 20 * np.log10(env + 1e-12)
    pk = int(np.argmax(env[pre:pre + int(0.08 * sr)]))
    after = db[pre + pk:]
    t20 = (pk + np.argmax(after < db[pre + pk] - 20)) / sr * 1000 if (after < db[pre + pk] - 20).any() else len(after) / sr * 1000
    t12 = (pk + np.argmax(after < db[pre + pk] - 12)) / sr * 1000 if (after < db[pre + pk] - 12).any() else 0
    zc = np.where(np.diff(np.signbit(wavg[pre:])))[0]
    sweep = []
    for i in range(1, len(zc)):
        t = (zc[i] + zc[i - 1]) / 2 / sr * 1000
        f = 0.5 / ((zc[i] - zc[i - 1]) / sr)
        if 30 < f < 700 and t < 80: sweep.append((round(t, 1), round(f, 1)))
    def f_at(ms):
        pts = [p for p in sweep if abs(p[0] - ms) < 6]
        return float(np.median([p[1] for p in pts])) if pts else 0.0
    kf = []
    for s in segs[:40]:
        a = pre + int(0.06 * sr); e = pre + int(min(0.2, t20 / 1000) * sr)
        if e - a > sr / 25:
            f, c = nsdf_pitch(butter(s, sr, 150, 'low')[a:e], sr, 30, 120)
            if f > 0 and c > 0.7: kf.append(f)
    f0 = float(np.median(kf)) if kf else 0.0
    # Click wie refana: Energie > 2 kHz zu < 300 Hz in den ersten 6 ms
    def band(x, lo, hi):
        X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
        fr = np.fft.rfftfreq(len(x), 1 / sr)
        return X[(fr >= lo) & (fr < hi)].sum()
    cl = []
    for i in range(first, min(nbeats, first + 40)):
        c = int(round(i * beat))
        seg = k[c - pre: c - pre + int(0.006 * sr)]
        cl.append(10 * np.log10(band(seg, 2000, 12000) + 1e-20) - 10 * np.log10(band(seg, 20, 300) + 1e-20))
    click = float(np.median(cl))

    # --- Bass: Noten finden (Pegelanstiege) ---
    eb = env_rms(b, sr, 2.5)
    thr = eb.max() * 10 ** (-30 / 20)
    on = np.where((eb[1:] > thr) & (eb[:-1] <= thr))[0]
    lens, cents = [], []
    start = int(first * beat)
    on = on[on > start]
    for i, o in enumerate(on[:200]):
        nxt = on[i + 1] if i + 1 < len(on) else len(eb)
        seg = eb[o:nxt]
        if len(seg) < 10: continue
        p_ = seg.max()
        above = np.where(seg > p_ * 10 ** (-12 / 20))[0]
        lens.append((above[-1] - above[0]) / sr * 1000)
        x = b[o: min(nxt, o + int(0.12 * sr))]
        if len(x) > 256:
            X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
            fr = np.fft.rfftfreq(len(x), 1 / sr)
            m = (fr > 60) & (fr < 3000)
            cents.append((X[m] * fr[m]).sum() / X[m].sum())
    # Bass-Spektralneigung (wie refana-Bänder): 120-250 und 250-1000 relativ zu 60-120 Hz
    E = np.zeros(3)
    for i, o in enumerate(on[:200]):
        nxt = on[i + 1] if i + 1 < len(on) else len(b)
        x = b[o: min(nxt, o + int(0.12 * sr))]
        if len(x) < 512: continue
        X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
        fr = np.fft.rfftfreq(len(x), 1 / sr)
        for j, (lo_, hi_) in enumerate([(60, 120), (120, 250), (250, 1000)]):
            E[j] += X[(fr >= lo_) & (fr < hi_)].sum()
    tilt1 = 10 * np.log10(E[1] / E[0]) if E[0] > 0 else 0
    tilt2 = 10 * np.log10(E[2] / E[0]) if E[0] > 0 else 0
    return dict(kick=dict(t12_ms=t12, t20_ms=t20, f0_hz=f0, f0_note=hz2note(f0)[0] if f0 else '-',
                          f_at_5=f_at(5), f_at_25=f_at(25), f_at_50=f_at(50), click_rel_db=click, sweep=sweep[:10]),
                bass=dict(note_len_ms=float(np.median(lens)) if lens else 0, centroid_hz=float(np.median(cents)) if cents else 0,
                          notes=len(lens), tilt_120_250=tilt1, tilt_250_1k=tilt2))

if __name__ == '__main__':
    import sys, json
    print(json.dumps(measure_stems(sys.argv[1], float(sys.argv[2])), indent=1, default=float))
