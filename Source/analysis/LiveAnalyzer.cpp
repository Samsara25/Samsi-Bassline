#include "LiveAnalyzer.h"

#include "../content/PitchDetect.h"

#include <cmath>
#include <numeric>

namespace samsi
{

namespace
{
    constexpr double kPiD = 3.14159265358979323846;
    inline double powToLufs (double p) { return p > 1.0e-20 ? -0.691 + 10.0 * std::log10 (p) : -120.0; }
    inline float lin2db (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }
}

//==============================================================================
void LoudnessMeter::prepare (double fs)
{
    // K-Gewichtung für beliebige Abtastraten (Analog-Prototyp nach BS.1770, bilinear transformiert)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPiD * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& b : pre)
            b = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                  2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPiD * f0 / fs), a0 = 1.0 + K / Q + K * K;
        for (auto& b : rlb)
            b = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
    hop = std::max (1, (int) std::lround (fs * 0.1));
    gatingBlocks.reserve (36000);
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& b : pre) b.z1 = b.z2 = 0.0;
    for (auto& b : rlb) b.z1 = b.z2 = 0.0;
    acc = 0.0; inHop = 0; nBlocks = 0; head = 0;
    blocks.fill (0.0);
    gatingBlocks.clear();
}

void LoudnessMeter::process (const float* l, const float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const double yl = rlb[0].tick (pre[0].tick (l[i]));
        const double yr = rlb[1].tick (pre[1].tick (r[i]));
        acc += yl * yl + yr * yr;
        if (++inHop >= hop)
        {
            blocks[(size_t) head] = acc / (double) hop;
            head = (head + 1) % (int) blocks.size();
            nBlocks = std::min (nBlocks + 1, (int) blocks.size());
            acc = 0.0; inHop = 0;
            if (nBlocks >= 4 && gatingBlocks.size() < gatingBlocks.capacity())
            {
                double s = 0.0;
                for (int k = 1; k <= 4; ++k) s += blocks[(size_t) ((head - k + (int) blocks.size()) % (int) blocks.size())];
                gatingBlocks.push_back (s / 4.0);
            }
        }
    }
}

double LoudnessMeter::momentaryLufs() const noexcept
{
    if (nBlocks < 4) return -120.0;
    double s = 0.0;
    for (int k = 1; k <= 4; ++k) s += blocks[(size_t) ((head - k + (int) blocks.size()) % (int) blocks.size())];
    return powToLufs (s / 4.0);
}

double LoudnessMeter::shortTermLufs() const noexcept
{
    if (nBlocks < (int) blocks.size()) return nBlocks >= 4 ? powToLufs (std::accumulate (blocks.begin(), blocks.end(), 0.0) / (double) nBlocks) : -120.0;
    return powToLufs (std::accumulate (blocks.begin(), blocks.end(), 0.0) / (double) blocks.size());
}

double LoudnessMeter::integratedLufs() const
{
    const double absGate = std::pow (10.0, (-70.0 + 0.691) / 10.0);
    double s = 0.0; int c = 0;
    for (double p : gatingBlocks) if (p > absGate) { s += p; ++c; }
    if (c == 0) return -120.0;
    const double relGate = (s / c) * std::pow (10.0, -10.0 / 10.0);
    double s2 = 0.0; int c2 = 0;
    for (double p : gatingBlocks) if (p > absGate && p > relGate) { s2 += p; ++c2; }
    return c2 > 0 ? powToLufs (s2 / c2) : -120.0;
}

//==============================================================================
TruePeak::TruePeak()
{
    constexpr int N = 4 * kTaps;
    const double beta = 6.0;
    auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 25; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
    std::array<double, N> proto {};
    for (int k = 0; k < N; ++k)
    {
        const double m = (double) k - (N - 1) * 0.5;
        const double x = m / 4.0;
        const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (kPiD * x) / (kPiD * x);
        const double r = 2.0 * (double) k / (double) (N - 1) - 1.0;
        proto[(size_t) k] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
    }
    for (int p = 0; p < 4; ++p)
    {
        double sum = 0.0;
        for (int j = 0; j < kTaps; ++j) sum += proto[(size_t) (4 * j + p)];
        for (int j = 0; j < kTaps; ++j) h[(size_t) p][(size_t) j] = (float) (proto[(size_t) (4 * j + p)] / sum);
    }
}

float TruePeak::process (float x) noexcept
{
    pos = (pos + 1) % kTaps;
    hist[(size_t) pos] = x;
    float m = 0.0f;
    for (int p = 0; p < 4; ++p)
    {
        float y = 0.0f;
        for (int j = 0; j < kTaps; ++j) y += h[(size_t) p][(size_t) j] * hist[(size_t) ((pos - j + kTaps) % kTaps)];
        m = std::max (m, std::abs (y));
    }
    return m;
}

//==============================================================================
void LiveAnalyzer::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& h : history) h.assign ((size_t) kHistory, 0.0f);
    flagHist.assign ((size_t) kHistory, 0u);
    for (auto& b : beat) b.assign ((size_t) kBeatBins, 0.0f);
    writePos = 0; total = 0; beatAge = 1 << 30;
    capLenKick = (int) (sr * 0.40);
    capLenBass = (int) (sr * 0.25);
    for (auto& c : kickCaps) { c.data.assign ((size_t) capLenKick, 0.0f); c.length = 0; c.serial = 0; }
    for (auto& c : bassCaps) { c.data.assign ((size_t) capLenBass, 0.0f); c.length = 0; c.serial = 0; }
    kickHead = bassHead = -1; kickFill = bassFill = 0;
    loud.prepare (sr);
    scHop = std::max (1, (int) std::lround (sr / 400.0));
    scFlux.assign (4800, 0.0);
    scFluxPos = scFluxCount = 0; scInHop = 0; scAcc = 0.0; scPrevE = 0.0;
    rmsCoef = std::exp (-1.0 / (0.3 * sr));
    resetMeters();
}

void LiveAnalyzer::resetMeters()
{
    loud.reset();
    tpL.reset(); tpR.reset();
    lv = {};
    msL = msR = sxy = sxx = syy = eM = eS = 0.0;
    holdPeakL = holdPeakR = 0.0f; holdL = holdR = 0;
    recentPeak = 0.0f; recentPeakAge = 0;
}

int LiveAnalyzer::drain (LiveTap& tap)
{
    std::array<TapFrame, 2048> buf;
    int got = 0;
    for (;;)
    {
        const int n = tap.pull (buf.data(), (int) buf.size());
        if (n <= 0) break;
        if (! frozen) feed (buf.data(), n);
        got += n;
    }
    if (! frozen)
    {
        lv.lufsM = loud.momentaryLufs();
        lv.lufsS = loud.shortTermLufs();
        lv.lufsI = loud.integratedLufs();
        if (total - lastTempoAt > (int64_t) (sr * 0.5)) { lastTempoAt = total; updateTempo(); }
    }
    return got;
}

void LiveAnalyzer::feed (const TapFrame* f, int n)
{
    const int hold = (int) (sr * 1.5);
    std::array<float, 512> bl, br;
    int blN = 0;

    for (int i = 0; i < n; ++i)
    {
        const auto& x = f[i];
        const float vals[NumTraces] = { x.mixL, x.mixR, x.kick, x.bass, x.scL, x.scR };
        const auto w = (size_t) (writePos & (kHistory - 1));
        for (int t = 0; t < NumTraces; ++t) history[(size_t) t][w] = vals[t];
        flagHist[w] = x.flags;
        writePos = (writePos + 1) & (kHistory - 1);
        ++total;

        // Beat-Modus: Wert in den Bin seiner Viertelposition
        if (x.beat >= 0.0f)
        {
            const int b = std::min (kBeatBins - 1, (int) (x.beat * (float) kBeatBins));
            for (int t = 0; t < NumTraces; ++t) beat[(size_t) t][(size_t) b] = vals[t];
            beatAge = 0;
        }
        else if (beatAge < (1 << 30)) ++beatAge;

        // Live-Mitschnitte
        if ((x.flags & 1u) != 0u) startCapture (kickCaps, kickHead, capLenKick, false), kickFill = capLenKick;
        if ((x.flags & 6u) != 0u) startCapture (bassCaps, bassHead, capLenBass, (x.flags & 4u) != 0u), bassFill = capLenBass;
        if (kickFill > 0 && kickHead >= 0)
        {
            auto& c = kickCaps[(size_t) kickHead];
            c.data[(size_t) c.length++] = x.kick;
            if (--kickFill == 0) finishCapture (c, true);
        }
        if (bassFill > 0 && bassHead >= 0)
        {
            auto& c = bassCaps[(size_t) bassHead];
            c.data[(size_t) c.length++] = x.bass;
            if (--bassFill == 0) finishCapture (c, false);
        }

        // Pegel (MIX)
        const float aL = std::abs (x.mixL), aR = std::abs (x.mixR);
        if (aL >= holdPeakL) { holdPeakL = aL; holdL = hold; } else if (--holdL <= 0) holdPeakL *= 0.9995f;
        if (aR >= holdPeakR) { holdPeakR = aR; holdR = hold; } else if (--holdR <= 0) holdPeakR *= 0.9995f;
        lv.samplePeakDb = std::max (lv.samplePeakDb, lin2db (std::max (aL, aR)));
        lv.truePeakDb = std::max (lv.truePeakDb, lin2db (std::max (tpL.process (x.mixL), tpR.process (x.mixR))));
        const double a = rmsCoef, b1 = 1.0 - rmsCoef;
        msL = a * msL + b1 * (double) x.mixL * x.mixL;
        msR = a * msR + b1 * (double) x.mixR * x.mixR;
        sxy = a * sxy + b1 * (double) x.mixL * x.mixR;
        const double m = 0.5 * ((double) x.mixL + x.mixR), s = 0.5 * ((double) x.mixL - x.mixR);
        eM = a * eM + b1 * m * m;
        eS = a * eS + b1 * s * s;
        recentPeak = std::max (recentPeak * 0.99995f, std::max (aL, aR));
        lv.scPeak = std::max (lv.scPeak * 0.9999f, std::max (std::abs (x.scL), std::abs (x.scR)));
        {
            const double sc = 0.5 * ((double) x.scL + x.scR);
            scAcc += sc * sc;
            if (++scInHop >= scHop)
            {
                const double e = std::log10 (1.0e-10 + scAcc / scHop);
                scFlux[(size_t) scFluxPos] = std::max (0.0, e - scPrevE);
                scPrevE = e;
                scFluxPos = (scFluxPos + 1) % (int) scFlux.size();
                scFluxCount = std::min (scFluxCount + 1, (int) scFlux.size());
                scAcc = 0.0; scInHop = 0;
            }
        }

        bl[(size_t) blN] = x.mixL; br[(size_t) blN] = x.mixR;
        if (++blN == (int) bl.size()) { loud.process (bl.data(), br.data(), blN); blN = 0; }
    }
    if (blN > 0) loud.process (bl.data(), br.data(), blN);

    lv.peakL = holdPeakL; lv.peakR = holdPeakR;
    lv.rmsL = (float) std::sqrt (msL); lv.rmsR = (float) std::sqrt (msR);
    const double msSum = 0.5 * (msL + msR);
    lv.rmsDb = msSum > 1.0e-12 ? (float) (10.0 * std::log10 (msSum)) : -120.0f;
    lv.crestDb = lin2db (recentPeak) - lv.rmsDb;
    const double den = std::sqrt (msL * msR);
    lv.correlation = den > 1.0e-12 ? (float) std::max (-1.0, std::min (1.0, sxy / den)) : 0.0f;
    lv.widthPct = eM > 1.0e-14 ? (float) std::min (400.0, 100.0 * std::sqrt (eS / eM)) : (eS > 1.0e-14 ? 400.0f : 0.0f);
    lv.balance = (msL + msR) > 1.0e-14 ? (float) ((msR - msL) / (msL + msR)) : 0.0f;
}

void LiveAnalyzer::startCapture (std::array<Capture, kCaptures>& caps, int& head, int, bool slide)
{
    if (head >= 0 && caps[(size_t) head].length > 0 && caps[(size_t) head].lengthMs <= 0.0f)
        finishCapture (caps[(size_t) head], &caps == &kickCaps);   // vorzeitig abschließen (nächste Note)
    head = (head + 1) % kCaptures;
    auto& c = caps[(size_t) head];
    c.length = 0; c.peakDb = -120.0f; c.lengthMs = 0.0f; c.pitchHz = 0.0f; c.serial = ++serial; c.slide = slide;
}

void LiveAnalyzer::finishCapture (Capture& c, bool isKick)
{
    float pk = 0.0f; int pkAt = 0;
    for (int i = 0; i < c.length; ++i) if (std::abs (c.data[(size_t) i]) > pk) { pk = std::abs (c.data[(size_t) i]); pkAt = i; }
    c.peakDb = lin2db (pk);
    int last = pkAt;
    for (int i = c.length - 1; i > pkAt; --i) if (std::abs (c.data[(size_t) i]) > pk * 0.1f) { last = i; break; }
    c.lengthMs = std::max (0.1f, (float) (1000.0 * last / sr));
    // Grundton: Kick im Ausklang (ab 60 ms), Bass im Notenkörper
    const int a = isKick ? (int) (sr * 0.06) : (int) (sr * 0.002);
    const int e = std::min (last, isKick ? (int) (sr * 0.25) : (int) (sr * 0.15));
    if (e - a > (int) (sr * 0.03) && pk > 1.0e-4f)
    {
        std::vector<float> seg (c.data.begin(), c.data.begin() + c.length);
        const auto r = detectPitch (seg, sr, a, e - a, 30.0, isKick ? 200.0 : 400.0);
        c.pitchHz = r.confidence > 0.6 ? (float) r.hz : 0.0f;
    }
}

namespace
{
    template <typename Caps>
    const LiveAnalyzer::Capture* pick (const Caps& caps, int head, int i, bool finishedOnly) noexcept
    {
        if (head < 0 || i < 0) return nullptr;
        int seen = 0;
        for (int k = 0; k < LiveAnalyzer::kCaptures; ++k)
        {
            const auto& c = caps[(size_t) ((head - k + LiveAnalyzer::kCaptures) % LiveAnalyzer::kCaptures)];
            if (c.serial <= 0) return nullptr;
            if (finishedOnly && c.lengthMs <= 0.0f) continue;
            if (seen++ == i) return &c;
        }
        return nullptr;
    }
}

const LiveAnalyzer::Capture* LiveAnalyzer::kickCapture (int i, bool finishedOnly) const noexcept { return pick (kickCaps, kickHead, i, finishedOnly); }
const LiveAnalyzer::Capture* LiveAnalyzer::bassCapture (int i, bool finishedOnly) const noexcept { return pick (bassCaps, bassHead, i, finishedOnly); }

int LiveAnalyzer::findTrigger (int trace, int window) const noexcept
{
    const int maxBack = (int) std::min<int64_t> (total, kHistory - 1);
    // 1. jüngster Kick-Start (Ereignis aus der Engine) – sample-genau
    for (int ago = window; ago < maxBack; ++ago)
        if ((histFlags (ago) & 1u) != 0u) return ago;
    // 2. steigende Flanke durch 0 mit Hysterese
    float pk = 0.0f;
    for (int ago = 0; ago < std::min (maxBack, window * 4); ++ago) pk = std::max (pk, std::abs (hist (trace, ago)));
    const float thr = pk * 0.05f;
    for (int ago = window; ago < maxBack - 1; ++ago)
        if (hist (trace, ago + 1) <= 0.0f && hist (trace, ago) > 0.0f && hist (trace, std::max (0, ago - 8)) > thr) return ago;
    return -1;
}

//==============================================================================
void LiveAnalyzer::computeSpectrum (int source, int order, Window w, std::vector<float>& outDb)
{
    order = std::min (14, std::max (10, order));
    const int N = 1 << order;
    if (fft == nullptr || fftOrderNow != order || winNow != w || (int) win.size() != N)
    {
        fft = std::make_unique<juce::dsp::FFT> (order);
        fftOrderNow = order; winNow = w;
        win.assign ((size_t) N, 1.0f);
        for (int i = 0; i < N; ++i)
        {
            const double x = 2.0 * kPiD * i / (N - 1);
            double v = 1.0;
            switch (w)
            {
                case Window::Hann:           v = 0.5 - 0.5 * std::cos (x); break;
                case Window::Hamming:        v = 0.54 - 0.46 * std::cos (x); break;
                case Window::BlackmanHarris: v = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x); break;
                case Window::Flattop:        v = 0.21557895 - 0.41663158 * std::cos (x) + 0.277263158 * std::cos (2 * x)
                                                 - 0.083578947 * std::cos (3 * x) + 0.006947368 * std::cos (4 * x); break;
                case Window::Rectangular:    v = 1.0; break;
            }
            win[(size_t) i] = (float) v;
        }
        winSum = std::accumulate (win.begin(), win.end(), 0.0f);
        fftBuf.assign ((size_t) N * 2, 0.0f);
    }
    for (int i = 0; i < N; ++i)
    {
        const int ago = N - 1 - i;
        float v;
        if (source == -1)      v = 0.5f * (hist (MixL, ago) + hist (MixR, ago));
        else if (source == -2) v = 0.5f * (hist (ScL, ago) + hist (ScR, ago));
        else                   v = hist (std::min (NumTraces - 1, std::max (0, source)), ago);
        fftBuf[(size_t) i] = v * win[(size_t) i];
    }
    std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    outDb.resize ((size_t) (N / 2 + 1));
    const float norm = 2.0f / winSum;
    for (int k = 0; k <= N / 2; ++k)
        outDb[(size_t) k] = lin2db (fftBuf[(size_t) k] * norm);
}

//==============================================================================
double LiveAnalyzer::estimateTempo (const std::vector<float>& x, double fs, double& conf)
{
    conf = 0.0;
    const int hop = std::max (1, (int) std::lround (fs / 400.0));     // 2,5 ms
    const int frames = (int) x.size() / hop;
    if (frames < 800) return 0.0;
    std::vector<double> e ((size_t) frames), flux ((size_t) frames, 0.0);
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        for (int i = 0; i < hop; ++i) { const double v = x[(size_t) (f * hop + i)]; s += v * v; }
        e[(size_t) f] = std::log10 (1.0e-10 + s / hop);
    }
    for (int f = 1; f < frames; ++f) flux[(size_t) f] = std::max (0.0, e[(size_t) f] - e[(size_t) f - 1]);
    return estimateTempoFromFlux (std::move (flux), fs / hop, conf);
}

double LiveAnalyzer::estimateTempoFromFlux (std::vector<double> flux, double hopsPerSec, double& conf)
{
    conf = 0.0;
    const int frames = (int) flux.size();
    if (frames < 800) return 0.0;
    const double mean = std::accumulate (flux.begin(), flux.end(), 0.0) / frames;
    for (auto& v : flux) v -= mean;
    {
        // leichte Glättung [1 2 1] -> genauere gebrochene Verzögerungen
        std::vector<double> sm (flux.size(), 0.0);
        for (size_t i = 1; i + 1 < flux.size(); ++i) sm[i] = 0.25 * flux[i - 1] + 0.5 * flux[i] + 0.25 * flux[i + 1];
        flux.swap (sm);
    }
    auto ac = [&] (double lag)   // Autokorrelation bei gebrochener Verzögerung (linear interpoliert)
    {
        const int l0 = (int) lag; const double fr = lag - l0;
        double s = 0.0;
        for (int f = 0; f + l0 + 1 < frames; ++f)
            s += flux[(size_t) f] * ((1.0 - fr) * flux[(size_t) (f + l0)] + fr * flux[(size_t) (f + l0 + 1)]);
        return s;
    };
    double zero = 0.0;
    for (auto v : flux) zero += v * v;
    if (zero <= 1.0e-12) return 0.0;
    auto score = [&] (double lag, int K) { double s = 0.0; for (int k = 1; k <= K; ++k) if (lag * k < frames - 10) s += ac (lag * k); return s; };
    // grob: 60..240 BPM in ganzen Hops (4 Perioden), dann fein mit möglichst vielen Perioden
    double bestLag = 0.0, best = -1.0e30;
    const int lagLo = (int) std::floor (hopsPerSec * 60.0 / 240.0), lagHi = (int) std::ceil (hopsPerSec * 60.0 / 60.0);
    for (int lag = lagLo; lag <= lagHi; ++lag) { const double sc = score (lag, 4); if (sc > best) { best = sc; bestLag = lag; } }
    // Oktav-/Triolen-Mehrdeutigkeit: kürzere Periode (L/2, 2L/3, 3L/4) bevorzugen, wenn sie fast genauso gut passt
    {
        const double base = ac (bestLag);
        for (double f : { 0.5, 2.0 / 3.0, 0.75 })
        {
            double cl = bestLag * f, cb = -1.0e30;
            for (double lag = cl - 1.0; lag <= cl + 1.0; lag += 0.1) { const double v = ac (lag); if (v > cb) { cb = v; cl = lag; } }
            if (cl >= lagLo && cb >= 0.7 * base) { bestLag = cl; break; }
        }
    }
    const int K = std::max (1, std::min (24, (int) ((frames - 10) / bestLag) - 1));
    {
        double c0 = bestLag, b0 = -1.0e30;
        for (double lag = c0 - 1.0; lag <= c0 + 1.0; lag += 0.01) { const double sc = score (lag, K); if (sc > b0) { b0 = sc; bestLag = lag; } }
    }
    conf = std::max (0.0, std::min (1.0, ac (bestLag) / zero));
    double bpm = 60.0 * hopsPerSec / bestLag;
    while (bpm < 90.0) bpm *= 2.0;      // Psytrance-Bereich bevorzugen
    while (bpm > 240.0) bpm *= 0.5;
    return bpm;
}

void LiveAnalyzer::updateTempo()
{
    if (scFluxCount < 1600 || lv.scPeak < 1.0e-4f) { scTempo = 0.0; scTempoConf = 0.0; return; }   // mind. 4 s
    std::vector<double> fl ((size_t) scFluxCount);
    const int sz = (int) scFlux.size();
    for (int i = 0; i < scFluxCount; ++i) fl[(size_t) i] = scFlux[(size_t) ((scFluxPos - scFluxCount + i + sz) % sz)];
    double conf = 0.0;
    const double t = estimateTempoFromFlux (std::move (fl), sr / scHop, conf);
    scTempo = conf > 0.2 ? t : 0.0;
    scTempoConf = conf;
}

} // namespace samsi
