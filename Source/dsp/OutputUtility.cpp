#include "OutputUtility.h"

namespace samsi
{

namespace
{
    // Koeffizienten a der Abschnitte (a - z^-2) / (1 - a z^-2), berechnet mit tools/hilbert_design.py
    // (Minimax-Optimierung der Phasendifferenz 90° zwischen 15 Hz und 20 kHz).
    constexpr double kA441[8] = { 0.049028326333, 0.346972268114, 0.657149328391, 0.844656443966,
                                  0.934302433803, 0.973150344698, 0.989493433311, 0.996714065064 };
    constexpr double kB441[8] = { 0.178345541933, 0.514715733712, 0.766352861235, 0.898412494103,
                                  0.957851467756, 0.983054183701, 0.993757153133, 0.998976784395 };
    constexpr double kA96[8]  = { 0.061192710929, 0.406848672658, 0.720850519442, 0.886296582926,
                                  0.956555380776, 0.983874732143, 0.994209468622, 0.998301433519 };
    constexpr double kB96[8]  = { 0.216620006575, 0.582492205768, 0.819758246943, 0.929362499668,
                                  0.973460480214, 0.990268318551, 0.996681059764, 0.999479459267 };
    constexpr double kA192[8] = { 0.115423296843, 0.552504496366, 0.815825021512, 0.930165312499,
                                  0.974534739411, 0.990939995955, 0.996874029073, 0.999109133965 };
    constexpr double kB192[8] = { 0.343703767375, 0.708620746530, 0.885844250218, 0.957676050928,
                                  0.984769971022, 0.994644437229, 0.998237521144, 0.999728874740 };
}

//==============================================================================
void HilbertPair::prepare (double sampleRate) noexcept
{
    const double* a = kA441; const double* b = kB441;
    if (sampleRate > 140000.0)     { a = kA192; b = kB192; }
    else if (sampleRate > 70000.0) { a = kA96;  b = kB96; }
    for (int i = 0; i < kSections; ++i) { ca[(size_t) i] = a[i]; cb[(size_t) i] = b[i]; }
    reset();
}

void HilbertPair::reset() noexcept
{
    for (auto& s : sa) s = {};
    for (auto& s : sb) s = {};
    xd = 0.0;
}

//==============================================================================
void FracDelay::prepare (int maxSamples)
{
    int n = 4;
    while (n < maxSamples + 8) n *= 2;
    buf.assign ((size_t) n, 0.0f);
    mask = n - 1;
    maxDelay = maxSamples;
    w = 0;
}

void FracDelay::reset() noexcept
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    w = 0;
}

//==============================================================================
void BusUtility::prepare (double sampleRate, int)
{
    sr = sampleRate;
    dcR = std::exp (-kTwoPi * 5.0 / sr);
    const int maxDel = (int) std::ceil ((kTimeShiftBaseMs * 2.0 + kMaxTiltMs + 2.0) * 0.001 * sr);
    for (int c = 0; c < 2; ++c)
    {
        dl[c].prepare (maxDel);
        dry[c].prepare ((int) std::ceil ((kTimeShiftBaseMs + 1.0) * 0.001 * sr));
        hil[c].prepare (sr);
    }
    for (auto* s : { &imager, &cosT, &sinT, &mix }) s->prepare (sr, 20.0);
    delL.prepare (sr, 60.0);
    delR.prepare (sr, 60.0);
    outL.prepare (sr, 20.0);
    outR.prepare (sr, 20.0);
    prepared = true;
    reset (UtilitySettings {});
}

void BusUtility::reset (const UtilitySettings& u) noexcept
{
    for (int c = 0; c < 2; ++c) { dl[c].reset(); dry[c].reset(); hil[c].reset(); dcX[c] = dcY[c] = 0.0; }
    for (auto& b : monoHp) b.z1 = b.z2 = 0.0;
    lastMonoHz = -1.0f;
    updateFilters (u);
    imager.reset (sanitize (u.imager, 0.0f, 4.0f, 1.0f));
    const double th = (double) sanitize (u.phaseDeg, 0.0f, 360.0f, 0.0f) * kPi / 180.0;
    cosT.reset ((float) std::cos (th));
    sinT.reset ((float) std::sin (th));
    mix.reset (sanitize (u.mix, 0.0f, 1.0f, 1.0f));
    delL.reset (0.0f);
    delR.reset (0.0f);
    lastBase = 0;
    outL.reset (dbToGain (sanitize (u.outLDb, -60.0f, 12.0f, 0.0f)));
    outR.reset (dbToGain (sanitize (u.outLink ? u.outLDb : u.outRDb, -60.0f, 12.0f, 0.0f)));
}

void BusUtility::updateFilters (const UtilitySettings& u) noexcept
{
    const float hz = sanitize (u.monoHz, 20.0f, 1000.0f, 120.0f);
    if (hz == lastMonoHz) return;
    lastMonoHz = hz;
    // Butterworth 4. Ordnung = zwei Biquads mit Q 0,5412 und 1,3066 (bilinear, vorverzerrt)
    const double K = std::tan (kPi * (double) hz / sr);
    const double qs[2] = { 0.54119610, 1.30656296 };
    for (int i = 0; i < 2; ++i)
    {
        const double q = qs[i];
        const double norm = 1.0 / (1.0 + K / q + K * K);
        auto& b = monoHp[i];
        b.b0 = norm; b.b1 = -2.0 * norm; b.b2 = norm;
        b.a1 = 2.0 * (K * K - 1.0) * norm;
        b.a2 = (1.0 - K / q + K * K) * norm;
    }
}

void BusUtility::process (float* l, float* r, int n, const UtilitySettings& uIn, int baseDelay) noexcept
{
    if (! prepared || n <= 0) return;
    UtilitySettings u = uIn;

    const double ms = 0.001 * sr;
    const double tilt = (double) sanitize (u.tiltMs, (float) -kMaxTiltMs, (float) kMaxTiltMs, 0.0f) * ms;
    const double ts = u.timeShiftOn ? (double) sanitize (u.timeShiftMs, -50.0f, 50.0f, 0.0f) * ms : 0.0;
    const double base = (double) baseDelay;
    const float tL = (float) std::max (0.0, base + ts + std::max (0.0, -tilt));
    const float tR = (float) std::max (0.0, base + ts + std::max (0.0, tilt));
    if (baseDelay != lastBase)
    {
        // Grundlatenz geändert (Time Shift ein/aus, Host gleicht neu aus): sofort springen statt gleiten
        delL.reset (tL); delR.reset (tR);
        lastBase = baseDelay;
    }
    delL.setTarget (tL);
    delR.setTarget (tR);
    imager.setTarget (sanitize (u.imager, 0.0f, 4.0f, 1.0f));
    const double th = (double) sanitize (u.phaseDeg, 0.0f, 360.0f, 0.0f) * kPi / 180.0;
    cosT.setTarget ((float) std::cos (th));
    sinT.setTarget ((float) std::sin (th));
    mix.setTarget (sanitize (u.mix, 0.0f, 1.0f, 1.0f));
    const float gl = dbToGain (sanitize (u.outLDb, -60.0f, 12.0f, 0.0f));
    outL.setTarget (gl);
    outR.setTarget (u.outLink ? gl : dbToGain (sanitize (u.outRDb, -60.0f, 12.0f, 0.0f)));
    if (u.monoOn) updateFilters (u);

    const bool smoothing = imager.isSmoothing() || cosT.isSmoothing() || sinT.isSmoothing() || mix.isSmoothing()
                           || delL.isSmoothing() || delR.isSmoothing() || outL.isSmoothing() || outR.isSmoothing();
    const bool identity = u.isIdentity() && baseDelay == 0 && ! smoothing && delL.getCurrent() == 0.0f && delR.getCurrent() == 0.0f;

    if (identity)
    {
        // exakt durchlassen; Verzögerungsleitungen weiter füllen, damit späteres Einschalten nahtlos ist
        for (int i = 0; i < n; ++i)
        {
            dl[0].push (l[i]); dl[1].push (r[i]);
            dry[0].push (l[i]); dry[1].push (r[i]);
        }
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i], inR = r[i];
        dry[0].push (inL); dry[1].push (inR);
        float xl = inL, xr = inR;

        if (u.dcBlock)
        {
            const double yl = (double) xl - dcX[0] + dcR * dcY[0]; dcX[0] = xl; dcY[0] = yl; xl = (float) yl;
            const double yr = (double) xr - dcX[1] + dcR * dcY[1]; dcX[1] = xr; dcY[1] = yr; xr = (float) yr;
        }

        double m = 0.5 * ((double) xl + (double) xr), s = 0.5 * ((double) xl - (double) xr);
        if (u.monoOn) s = monoHp[1].tick (monoHp[0].tick (s));
        s *= (double) imager.next();
        xl = (float) (m + s); xr = (float) (m - s);

        dl[0].push (xl); dl[1].push (xr);
        xl = dl[0].read ((double) delL.next());
        xr = dl[1].read ((double) delR.next());

        const float c = cosT.next(), sn = sinT.next();
        if (u.phaseOn)
        {
            float a, b;
            hil[0].process (xl, a, b); xl = c * a - sn * b;
            hil[1].process (xr, a, b); xr = c * a - sn * b;
        }

        if (u.swap) std::swap (xl, xr);
        if (u.invertL) xl = -xl;
        if (u.invertR) xr = -xr;

        switch (u.listen)
        {
            case 1: xr = 0.0f; break;
            case 2: xl = 0.0f; break;
            case 3: { const float mm = 0.5f * (xl + xr); xl = xr = mm; break; }
            case 4: { const float ss = 0.5f * (xl - xr); xl = xr = ss; break; }
            case 5: xr = xl; break;
            case 6: xl = xr; break;
            default: break;
        }

        // Mix: Gleichleistungs-Überblendung mit latenzgleichem Trockensignal
        const float mx = mix.next();
        if (mx < 1.0f)
        {
            const float gw = std::sin (mx * (float) kPi * 0.5f), gd = std::cos (mx * (float) kPi * 0.5f);
            xl = gw * xl + gd * dry[0].read (base);
            xr = gw * xr + gd * dry[1].read (base);
        }

        l[i] = xl * outL.next();
        r[i] = xr * outR.next();
        if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
        {
            l[i] = r[i] = 0.0f;
            reset (u);
        }
    }
}

//==============================================================================
void Limiter::prepare (double sampleRate)
{
    la = latencySamples (sampleRate);
    dL.assign ((size_t) la, 0.0f);
    dR.assign ((size_t) la, 0.0f);
    gHist.assign ((size_t) la + 1, 1.0f);
    boxHist.assign ((size_t) la, 1.0f);
    rel = (float) (1.0 - std::exp (-1.0 / (0.08 * sampleRate)));   // 80 ms Release
    reset();
}

void Limiter::reset() noexcept
{
    std::fill (dL.begin(), dL.end(), 0.0f);
    std::fill (dR.begin(), dR.end(), 0.0f);
    std::fill (gHist.begin(), gHist.end(), 1.0f);
    std::fill (boxHist.begin(), boxHist.end(), 1.0f);
    boxSum = (double) la;
    pos = 0;
    gr = 1.0f;
    grDb = 0.0f;
}

void Limiter::process (float* l, float* r, int n, float ceilingDb) noexcept
{
    if (dL.empty()) return;
    const float ceil = dbToGain (sanitize (ceilingDb, -24.0f, 0.0f, -0.2f)) * 0.99995f;
    const int H = (int) gHist.size();
    float minGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float pk = std::max (std::abs (l[i]), std::abs (r[i]));
        const float need = pk > ceil ? ceil / pk : 1.0f;
        // Min-Filter über die letzten la+1 benötigten Verstärkungen (Ringpuffer, Brute-Force: la ist klein)
        gHist[(size_t) (pos % H)] = need;
        float m = 1.0f;
        for (int k = 0; k < H; ++k) m = std::min (m, gHist[(size_t) k]);
        // Release: steigt langsam, fällt sofort (bleibt <= m)
        gr = std::min (m, gr + (1.0f - gr) * rel);
        // gleitender Mittelwert über la Werte
        const int bi = pos % la;
        boxSum += (double) gr - (double) boxHist[(size_t) bi];
        boxHist[(size_t) bi] = gr;
        const float g = (float) std::min (1.0, boxSum / (double) la);

        const float outL = dL[(size_t) bi], outR = dR[(size_t) bi];
        dL[(size_t) bi] = l[i];
        dR[(size_t) bi] = r[i];
        float yl = outL * g, yr = outR * g;
        // Sicherheit gegen Rundung (wirkt nur im Bereich weniger 1e-6)
        yl = std::min (ceil, std::max (-ceil, yl));
        yr = std::min (ceil, std::max (-ceil, yr));
        l[i] = yl; r[i] = yr;
        minGain = std::min (minGain, g);
        ++pos;
        if (pos >= la * H * 1024) pos = pos % (la * H);   // Überlauf vermeiden, Modulo-Lage beibehalten
    }
    grDb = (float) gainToDb (minGain);
}

} // namespace samsi
