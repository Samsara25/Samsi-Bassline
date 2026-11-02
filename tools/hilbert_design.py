import numpy as np
from scipy.optimize import least_squares
# Struktur (Niemitalo): Zweig A = prod (a_i^2 - z^-2)/(1 - a_i^2 z^-2); Zweig B = z^-1 * prod (...)
def phase(c, w):
    z2 = np.exp(-2j*w)
    H = np.ones_like(w, dtype=complex)
    for a in c:
        H *= (a - z2)/(1 - a*z2)
    return np.unwrap(np.angle(H))
def diff(x, w, n):
    ca, cb = x[:n], x[n:]
    pa = phase(ca, w); pb = phase(cb, w) - w
    d = pa - pb
    return d
def err(x, w, n):
    d = diff(x, w, n)
    # Ziel: konstante 90 Grad (Vorzeichen egal, mod 2pi)
    d = np.mod(d, 2*np.pi)
    return (d - np.pi/2)
def design(fs, n, lo=15.0, hi=20000.0):
    hi = min(hi, 0.45*fs)
    w = 2*np.pi*np.geomspace(lo, hi, 600)/fs
    best=None
    rng=np.random.default_rng(1)
    for trial in range(40):
        x0 = np.sort(rng.uniform(0.3, 0.99999, 2*n))
        x0 = np.concatenate([x0[0::2], x0[1::2]])
        r = least_squares(lambda x: err(x,w,n), x0, bounds=(0.0, 0.9999999))
        m = np.max(np.abs(err(r.x,w,n)))
        if best is None or m < best[0]: best=(m, r.x)
    m,x = best
    # minimax-Verfeinerung über p-Norm
    for p in (4, 8, 16):
        r = least_squares(lambda y: np.abs(err(y,w,n))**(p/2), x, bounds=(0.0,0.9999999))
        if np.max(np.abs(err(r.x,w,n))) < m: m, x = np.max(np.abs(err(r.x,w,n))), r.x
    return np.degrees(m), x
for fs in (44100, 96000, 192000):
    for n in (6, 8):
        m, x = design(fs, n)
        print(fs, n, 'max error deg %.3f' % m, np.round(np.sort(x[:n]),12).tolist(), np.round(np.sort(x[n:]),12).tolist())
