#include "BassEngine.h"

namespace samsi
{

//==============================================================================
static double timeCoef (double samples) noexcept
{
    // erreicht ~99 % des Zielwerts nach 'samples'
    return std::exp (-4.605170186 / std::max (1.0, samples));
}

void BassEngine::Env::start (double attackSamples, double decaySamples, double sus, double releaseSamples) noexcept
{
    stage = Stage::Attack;
    level = 0.0;
    attackInc = 1.0 / std::max (1.0, attackSamples);
    setTimes (decaySamples, sus, releaseSamples);
}

void BassEngine::Env::setTimes (double decaySamples, double sus, double releaseSamples) noexcept
{
    decayCoef = timeCoef (decaySamples);
    sustain = sus;
    releaseCoef = timeCoef (releaseSamples);
}

double BassEngine::Env::tick() noexcept
{
    switch (stage)
    {
        case Stage::Attack:
            level += attackInc;
            if (level >= 1.0) { level = 1.0; stage = Stage::Decay; }
            break;
        case Stage::Decay:
            level = sustain + (level - sustain) * decayCoef;
            break;
        case Stage::Release:
            level *= releaseCoef;
            if (level < 1.0e-5) { level = 0.0; stage = Stage::Off; }
            break;
        case Stage::Off:
            level = 0.0;
            break;
    }
    return level;
}

//==============================================================================
void BassEngine::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto* sm : { &polarity, &width })
        sm->prepare (sr, 25.0);
    trim.prepare (sr, 25.0);
    level.prepare (sr, 25.0);
    drive.prepare (sr, 20.0);

    delaySamples = std::max (1, msToSamples (1.5, sr));
    delayLine.assign ((size_t) delaySamples + 1, 0.0f);   // Allokation nur hier, nie im Audio-Thread
    for (auto& b : pickBuffers) b.assign ((size_t) kPickBufSize, 0.0f);
    reset();
}

void BassEngine::reset() noexcept
{
    for (auto& v : voices) v = Voice {};
    current = 0;
    polarity.reset (1.0f);
    trim.reset (1.0f);
    level.reset (dbToGain (-3.0f));
    drive.reset ((float) driveDbToFactor (3.0));
    width.reset (0.0f);
    duckLp.reset();
    widthHp.reset();
    duckLpHz = widthHpHz = -1.0f;
    duckT = -1;
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    delayWrite = 0;
    primed = false;
}

void BassEngine::noteOn (int midiNote, float velocity, bool slide, int source, int id,
                         double startPhase01, const BassSettings& s) noexcept
{
    midiNote = std::min (127, std::max (0, midiNote));
    const double targetLog = std::log (440.0 * std::pow (2.0, (midiNote - 69) / 12.0));
    const double ms = sr * 0.001;

    auto& cur = voices[current];
    const bool canSlide = slide && cur.active && cur.fadeStep <= 0.0f && cur.amp.stage != Stage::Off;

    if (canSlide)
    {
        // gebundene Note: kein Retrigger, kein Phasenreset, Glide zur neuen Tonhöhe
        cur.targetLogFreq = targetLog;
        cur.note = midiNote;
        cur.gate = true;
        cur.source = source;
        cur.id = id;
        if (cur.amp.stage == Stage::Release)  cur.amp.stage = Stage::Decay;
        if (cur.fenv.stage == Stage::Release) cur.fenv.stage = Stage::Decay;
        return;
    }

    double carriedPhase = cur.phase, carriedSub = cur.subPhase;

    if (cur.active)
    {
        cur.fadeStep = cur.fade / (float) std::max (1, msToSamples (2.0, sr));
        current ^= 1;
    }

    auto& v = voices[current];
    v = Voice {};
    v.active = true;
    v.gate = true;
    v.source = source;
    v.id = id;
    v.note = midiNote;
    v.velocity = sanitize (velocity, 0.0f, 1.0f, 0.8f);
    v.logFreq = v.targetLogFreq = targetLog;

    v.startPhase01 = wrap01 (startPhase01);
    if (s.phaseReset)
    {
        v.phase = wrap01 (startPhase01);
        v.subPhase = v.phase;
    }
    else
    {
        // freilaufend, aber weiterhin deterministisch (keine Zufallsphase)
        v.phase = carriedPhase;
        v.subPhase = carriedSub;
    }

    v.amp.start  (sanitize (s.attackMs, 0.05f, 200.0f, 0.5f) * ms,
                  sanitize (s.decayMs, 1.0f, 5000.0f, 140.0f) * ms,
                  sanitize (s.sustain, 0.0f, 1.0f, 0.6f),
                  sanitize (s.releaseMs, 0.5f, 2000.0f, 8.0f) * ms);
    v.fenv.start (sanitize (s.fAttackMs, 0.05f, 200.0f, 0.3f) * ms,
                  sanitize (s.fDecayMs, 1.0f, 5000.0f, 90.0f) * ms,
                  sanitize (s.fSustain, 0.0f, 1.0f, 0.0f),
                  sanitize (s.fReleaseMs, 0.5f, 2000.0f, 20.0f) * ms);
}

void BassEngine::noteOff (int source, int id) noexcept
{
    auto& v = voices[current];
    if (v.active && v.gate && v.source == source && v.id == id)
    {
        v.gate = false;
        v.amp.gateOff();
        v.fenv.gateOff();
    }
}

void BassEngine::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.active)
        {
            v.gate = false;
            v.amp.gateOff();
            v.fenv.gateOff();
        }
}

void BassEngine::triggerDuck (float depthDb, float timeMs) noexcept
{
    duckDepthDb = sanitize (depthDb, 0.0f, 24.0f, 0.0f);
    duckRise = std::max (1.0, 0.5 * sr * 0.001);
    duckLen  = std::max (1.0, (double) sanitize (timeMs, 1.0f, 1000.0f, 80.0f) * sr * 0.001);
    duckT = duckDepthDb > 0.0 ? 0 : -1;
}

double BassEngine::renderVoice (Voice& v, float* pickBuf, const BassSettings& s, double glideCoef, double driveFactor,
                                const WavetableData* wt, const KickSample* smp) noexcept
{
    // Glide im log-Frequenzbereich
    v.logFreq = v.targetLogFreq + (v.logFreq - v.targetLogFreq) * glideCoef;
    const double f = std::exp (v.logFreq);
    const double dt = std::min (0.45, f / sr);

    const double t = v.phase;
    double osc = 0.0;

    if (s.saw > 0.0f)
        osc += s.saw * (1.0 - 2.0 * t + polyBlep (t, dt));          // fallender Saw: Grundwelle phasengleich zu sin

    if (s.square > 0.0f)
    {
        const double pw = std::min (0.95, std::max (0.05, (double) s.pulseWidth));
        double sq = t < pw ? 1.0 : -1.0;
        sq += polyBlep (t, dt);
        sq -= polyBlep (wrap01 (t + 1.0 - pw), dt);
        osc += s.square * sq;
    }

    if (s.sub > 0.0f)
        osc += s.sub * std::sin (kTwoPi * (s.subOctave < 0 ? v.subPhase : v.phase));

    // Wavetable: gleiche Phase wie der klassische Oszillator (Phasenreset/Startphase gelten auch hier)
    const bool wtOn = wt != nullptr && s.wtLevel > 0.0f && wt->numFrames > 0;
    if (wtOn)
    {
        const double pos = (double) s.wtPos + (double) s.wtEnvAmt * v.lastFEnv;
        osc += s.wtLevel * wt->read (pos, t, v.wtL);
    }

    // Bass-Sample: auf die Notenfrequenz gestimmt; Startpunkt = Start + Phase x Sample-Periode.
    // Moduliert von Glide, Filter-Hüllkurve (Tonhöhe), und – wenn "durch Filter" – Pick, Dämpfer, Filter, Drive.
    const bool smpOn = smp != nullptr && s.smpLevel > 0.0f && smp->data.size() > 8 && smp->rate > 0.0;
    double smpSig = 0.0;
    if (smpOn)
    {
        const double root = smp->pitchHz > 0.0f ? (double) smp->pitchHz : 65.406;   // unbekannt -> C2
        if (v.smpPos < 0.0)
            v.smpPos = (double) s.smpStartMs * 0.001 * smp->rate + v.startPhase01 * smp->rate / root;
        const auto& d = smp->data;
        const int n = (int) d.size();
        const int i = (int) v.smpPos;
        if (i < n - 2)
        {
            const float fr = (float) (v.smpPos - (double) i);
            const float xm1 = d[(size_t) std::max (0, i - 1)], x0 = d[(size_t) i], x1 = d[(size_t) i + 1], x2 = d[(size_t) i + 2];
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            smpSig = (double) s.smpLevel * (double) (((c3 * fr + c2) * fr + c1) * fr + x0);
            const double st = (double) s.smpTuneSt + (double) s.smpPitchEnvSt * v.lastFEnv;
            v.smpPos += f / root * smp->rate / sr * std::exp2 (st / 12.0);
        }
    }

    // Mischung normieren, sobald die Summe der Anteile 1 übersteigt (verhindert Pegelexplosion)
    const double norm = 1.0 / std::max (1.0, (double) s.saw + (double) s.square + (double) s.sub + (wtOn ? (double) s.wtLevel : 0.0)
                                             + (smpOn ? (double) s.smpLevel : 0.0));
    if (s.smpThru) { osc += smpSig; smpSig = 0.0; }
    osc *= norm;
    smpSig *= norm;

    // Pick: Kammfilter mit Verzögerung = Anschlagposition x Periode (Kerben bei Vielfachen von 1/pos)
    if (s.pickAmount > 0.0f && pickBuf != nullptr)
    {
        const double D = std::min ((double) kPickBufSize - 4.0, std::max (1.0, (double) s.pickPos * sr / f));
        pickBuf[v.pickWrite] = (float) osc;
        double delayed = 0.0;
        if ((double) v.pickT >= D)
        {
            const double rp = (double) v.pickWrite - D;
            const double rpw = rp < 0.0 ? rp + kPickBufSize : rp;
            const int i0 = (int) rpw;
            const double fr = rpw - (double) i0;
            const float a = pickBuf[i0 & (kPickBufSize - 1)], b = pickBuf[(i0 + 1) & (kPickBufSize - 1)];
            delayed = (double) a + fr * (double) (b - a);
        }
        v.pickWrite = (v.pickWrite + 1) & (kPickBufSize - 1);
        ++v.pickT;
        osc -= (double) s.pickAmount * delayed;
    }

    // Dämpfer: Tiefpass, dessen Grenzfrequenz nach dem Anschlag abfällt (Höhen klingen schneller ab)
    if (s.damper > 0.0f)
    {
        osc = v.dampState + v.dampCoef * (osc - v.dampState);
        v.dampState = osc;
    }

    v.phase += dt;
    if (v.phase >= 1.0) v.phase -= 1.0;
    v.subPhase += dt * 0.5;
    if (v.subPhase >= 1.0) v.subPhase -= 1.0;

    // Hüllkurven
    const double ampEnv = v.amp.tick();
    const double fEnv   = v.fenv.tick();

    // Filter: Koeffizienten mit Steuerrate (alle 4 Samples) – spart tan()/pow(), deterministisch
    v.lastFEnv = fEnv;
    if (v.ctrlCounter-- <= 0)
    {
        v.ctrlCounter = 3;
        if (wtOn) v.wtL = WavetableData::levelFor (f, sr);
        if (s.damper > 0.0f)
        {
            const double tau = (0.4 - 0.385 * (double) s.damper) * sr;            // 400 ms .. 15 ms
            const double tSec = (double) v.pickT > 0.0 ? (double) v.pickT : (double) v.ctrlTicks * 4.0;
            const double fcD = std::min (sr * 0.45, f * (2.0 + 60.0 * std::exp (-tSec / tau)));
            v.dampCoef = 1.0 - std::exp (-kTwoPi * fcD / sr);
        }
        ++v.ctrlTicks;
        const double velF = 1.0 - s.velToFilter * (1.0 - v.velocity);
        const double oct = (double) s.envAmountOct * fEnv * velF + (double) s.keytrack * (double) (v.note - 60) / 12.0;
        const double fc = std::min (sr * 0.45, std::max (20.0, (double) s.cutoffHz * std::exp2 (oct)));
        const double res = std::min (1.0, std::max (0.0, (double) s.resonance));
        const double g = SvfCoeffs::prewarp (fc, sr);
        const double kRes = 2.0 - 1.9 * res;              // Q max. ~10
        v.c1.setG (g, s.filterMode == FilterMode::LP24 ? 1.41421356 : kRes);
        v.c2.setG (g, kRes);
        v.resoComp = 1.0 / (1.0 + 1.5 * res);             // Resonanzanhebung teilweise ausgleichen
    }

    double y = svfTick (v.f1, v.c1, osc * v.resoComp).lp;
    if (s.filterMode == FilterMode::LP24)
        y = svfTick (v.f2, v.c2, y).lp;

    y = v.shaper.process (y * 0.7, driveFactor);
    y += smpSig * 0.7;   // Sample ohne Filter/Drive (nur Amp-Hüllkurve, Velocity)

    const double velA = 1.0 - s.velToAmp * (1.0 - v.velocity);
    y *= ampEnv * velA;

    if (v.fadeStep > 0.0f)
    {
        y *= v.fade;
        v.fade -= v.fadeStep;
        if (v.fade <= 0.0f) { v.fade = 0.0f; v.active = false; }
    }
    if (v.amp.stage == Stage::Off)
        v.active = false;

    return y;
}

void BassEngine::render (float* left, float* right, int n, const BassSettings& liveIn, const FitSettings& fit,
                         const WavetableData* wt, const KickSample* smp) noexcept
{
    BassSettings s = liveIn;
    s.saw = sanitize (s.saw, 0.0f, 1.0f, 0.8f);
    s.square = sanitize (s.square, 0.0f, 1.0f, 0.0f);
    s.sub = sanitize (s.sub, 0.0f, 1.0f, 0.5f);
    s.cutoffHz = sanitize (s.cutoffHz, 20.0f, 20000.0f, 900.0f);
    s.envAmountOct = sanitize (s.envAmountOct, -8.0f, 8.0f, 2.5f);
    s.keytrack = sanitize (s.keytrack, 0.0f, 1.0f, 0.5f);
    s.velToAmp = sanitize (s.velToAmp, 0.0f, 1.0f, 0.3f);
    s.velToFilter = sanitize (s.velToFilter, 0.0f, 1.0f, 0.4f);
    s.wtLevel = sanitize (s.wtLevel, 0.0f, 1.0f, 0.0f);
    s.wtPos = sanitize (s.wtPos, 0.0f, 1.0f, 0.0f);
    s.wtEnvAmt = sanitize (s.wtEnvAmt, -1.0f, 1.0f, 0.0f);
    s.pickAmount = sanitize (s.pickAmount, 0.0f, 1.0f, 0.0f);
    s.pickPos = sanitize (s.pickPos, 0.02f, 0.5f, 0.2f);
    s.damper = sanitize (s.damper, 0.0f, 1.0f, 0.0f);
    s.smpLevel = sanitize (s.smpLevel, 0.0f, 1.0f, 0.0f);
    s.smpTuneSt = sanitize (s.smpTuneSt, -24.0f, 24.0f, 0.0f);
    s.smpStartMs = sanitize (s.smpStartMs, 0.0f, 500.0f, 0.0f);
    s.smpPitchEnvSt = sanitize (s.smpPitchEnvSt, -24.0f, 24.0f, 0.0f);
    const bool havePick = ! pickBuffers[0].empty() && ! pickBuffers[1].empty();

    const double glideMs = sanitize (s.glideMs, 0.0f, 2000.0f, 40.0f);
    const double glideCoef = glideMs <= 0.01 ? 0.0 : timeCoef (glideMs * sr * 0.001);

    // laufende Noten übernehmen Decay/Sustain/Release-Änderungen sanft
    for (auto& v : voices)
        if (v.active)
        {
            v.amp.setTimes  (sanitize (s.decayMs, 1.0f, 5000.0f, 140.0f) * sr * 0.001,
                             sanitize (s.sustain, 0.0f, 1.0f, 0.6f),
                             sanitize (s.releaseMs, 0.5f, 2000.0f, 8.0f) * sr * 0.001);
            v.fenv.setTimes (sanitize (s.fDecayMs, 1.0f, 5000.0f, 90.0f) * sr * 0.001,
                             sanitize (s.fSustain, 0.0f, 1.0f, 0.0f),
                             sanitize (s.fReleaseMs, 0.5f, 2000.0f, 20.0f) * sr * 0.001);
        }

    const bool fitOn = fit.enabled;
    polarity.setTarget (fitOn && fit.polarity ? -1.0f : 1.0f);
    trim.setTarget (fitOn ? dbToGain (sanitize (fit.trimDb, -6.0f, 6.0f, 0.0f)) : 1.0f);
    level.setTarget (dbToGain (sanitize (s.levelDb, -100.0f, 12.0f, -3.0f)));
    drive.setTarget ((float) driveDbToFactor (sanitize (s.driveDb, 0.0f, 36.0f, 3.0f)));
    width.setTarget (sanitize (s.width, 0.0f, 1.0f, 0.0f));
    if (! primed)
    {
        polarity.reset (fitOn && fit.polarity ? -1.0f : 1.0f);
        trim.reset (fitOn ? dbToGain (sanitize (fit.trimDb, -6.0f, 6.0f, 0.0f)) : 1.0f);
        level.reset (dbToGain (sanitize (s.levelDb, -100.0f, 12.0f, -3.0f)));
        drive.reset ((float) driveDbToFactor (sanitize (s.driveDb, 0.0f, 36.0f, 3.0f)));
        width.reset (sanitize (s.width, 0.0f, 1.0f, 0.0f));
        primed = true;
    }

    const float dHz = sanitize (fit.duckHz, 30.0f, 1000.0f, 150.0f);
    if (std::abs (dHz - duckLpHz) > 1.0e-6f) { duckLpHz = dHz; duckLpC.set (dHz, sr, 1.41421356); }
    const float wHz = sanitize (s.widthHz, 50.0f, 5000.0f, 250.0f);
    if (std::abs (wHz - widthHpHz) > 1.0e-6f) { widthHpHz = wHz; widthHpC.set (wHz, sr, 1.41421356); }

    const bool lowOnly = fit.duckLowOnly;

    for (int i = 0; i < n; ++i)
    {
        const double drv = drive.next();
        double x = 0.0;
        for (int vi = 0; vi < 2; ++vi)
            if (voices[vi].active)
                x += renderVoice (voices[vi], havePick ? pickBuffers[vi].data() : nullptr, s, glideCoef, drv, wt, smp);

        x *= (double) polarity.next() * (double) trim.next();

        // Ducking (deterministisch, ausgelöst vom Kick-Trigger)
        const double low = svfTick (duckLp, duckLpC, x).lp;
        if (duckT >= 0)
        {
            const double td = (double) duckT;
            double d;
            if (td < duckRise) d = td / duckRise;
            else
            {
                const double xx = (td - duckRise) / duckLen;
                d = xx < 1.0 ? (1.0 - xx) * (1.0 - xx) : 0.0;
            }
            const double g = std::pow (10.0, -duckDepthDb * d / 20.0);
            x = lowOnly ? x - (1.0 - g) * low : x * g;
            if (++duckT > (int64_t) (duckRise + duckLen)) duckT = -1;
        }

        x *= (double) level.next();

        // Breite: Seitensignal nur aus Hochpass-Anteil; L+R bleibt = 2x
        const double hp = svfTick (widthHp, widthHpC, x).hp;
        const float w = width.next();
        float side = 0.0f;
        if (! delayLine.empty())
        {
            const int readPos = (delayWrite + 1) % (int) delayLine.size();
            side = w * delayLine[(size_t) readPos];
            delayLine[(size_t) delayWrite] = (float) hp;
            delayWrite = readPos;
        }

        left[i]  = (float) x + side;
        right[i] = (float) x - side;
    }
}

} // namespace samsi
