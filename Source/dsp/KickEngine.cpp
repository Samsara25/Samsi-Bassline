#include "KickEngine.h"

namespace samsi
{

void KickEngine::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    drive.prepare (sr, 20.0);
    reset();
}

void KickEngine::reset() noexcept
{
    for (auto& v : voices) v = Voice {};
    for (auto& v : svoices) v = SampleVoice {};
    current = 0;
    scurrent = 0;
    smpHp1.reset();
    smpHp2.reset();
    smpHpHz = -1.0f;
    shaper.reset();
    drive.reset ((float) driveDbToFactor (4.0));
    primed = false;
    triggerCount = 0;
}

double KickEngine::totalLengthMs (const KickSettings& s, float tailScale) noexcept
{
    const double ts = std::min (1.0, std::max (0.3, (double) tailScale));
    double len = (double) s.attackMs + (double) s.holdMs + ts * ((double) s.decayMs + (double) s.tailMs);
    if (s.smpOn)
        len = std::max (len, (double) s.smpOffsetMs + (double) s.smpLengthMs + (double) s.smpFadeMs);
    return len;
}

void KickEngine::validateSample (const KickSample* currentSample) noexcept
{
    for (auto& v : svoices)
        if (v.active && v.smp != currentSample)
            v = SampleVoice {};
}

float KickEngine::SampleVoice::tick (double rateMul, float envGain) noexcept
{
    if (delay > 0) { --delay; return 0.0f; }

    const auto& d = smp->data;
    const int n = (int) d.size();
    const int i = (int) pos;
    if (i >= n - 2 || pos < 0.0)
    {
        active = false;
        return 0.0f;
    }

    // kubische Hermite-Interpolation
    const float frac = (float) (pos - (double) i);
    const float xm1 = d[(size_t) std::max (0, i - 1)], x0 = d[(size_t) i], x1 = d[(size_t) i + 1], x2 = d[(size_t) i + 2];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    const float s = ((c3 * frac + c2) * frac + c1) * frac + x0;

    const double td = (double) t;
    double env;
    if (td < fadeIn)        env = td / fadeIn;
    else if (td < holdLen)  env = 1.0;
    else
    {
        const double x = (td - holdLen) / fadeLen;
        if (x >= 1.0) { active = false; return 0.0f; }
        env = (1.0 - x) * (1.0 - x);
    }

    float out = s * gain * (float) env * envGain;
    if (fadeStep > 0.0f)
    {
        out *= fade;
        fade -= fadeStep;
        if (fade <= 0.0f) { active = false; fade = 0.0f; }
    }

    pos += step * rateMul;
    ++t;
    return out;
}

void KickEngine::trigger (const KickSettings& s, float velocity, float tailScale, const KickSample* smp) noexcept
{
    // Sample-Layer
    if (svoices[scurrent].active)
    {
        auto& old = svoices[scurrent];
        old.fadeStep = old.fade / (float) std::max (1, msToSamples (1.5, sr));
        scurrent ^= 1;
    }
    if (s.smpOn && smp != nullptr && smp->data.size() > 4 && smp->rate > 0.0)
    {
        auto& v = svoices[scurrent];
        v = SampleVoice {};
        v.active = true;
        v.smp = smp;
        v.delay = msToSamples (sanitize (s.smpOffsetMs, 0.0f, 30.0f, 0.0f), sr);
        v.pos = (double) sanitize (s.smpStartMs, 0.0f, 500.0f, 0.0f) * 0.001 * smp->rate;
        v.step = smp->rate / sr * std::pow (2.0, (double) sanitize (s.smpTuneSt, -24.0f, 24.0f, 0.0f) / 12.0);
        if (s.smpKeyTrack && smp->pitchHz > 0.0f)   // Sample-Grundton auf den Kick-Grundton
            v.step *= (double) sanitize (s.endHz, 15.0f, 400.0f, 46.25f) / (double) smp->pitchHz;
        v.fadeIn = std::max (1.0, 0.0003 * sr);
        v.holdLen = std::max (v.fadeIn, (double) sanitize (s.smpLengthMs, 1.0f, 5000.0f, 400.0f) * 0.001 * sr);
        v.fadeLen = std::max (1.0, (double) sanitize (s.smpFadeMs, 0.5f, 5000.0f, 60.0f) * 0.001 * sr);
        v.gain = dbToGain (sanitize (s.smpLevelDb, -100.0f, 12.0f, 0.0f)) * sanitize (velocity, 0.0f, 1.0f, 1.0f)
                 * (s.smpInvert ? -1.0f : 1.0f);
    }

    // laufende Stimme sanft ausblenden statt hart abschneiden
    if (voices[current].active)
    {
        auto& old = voices[current];
        old.fadeStep = old.fade / (float) std::max (1, msToSamples (1.5, sr));
        current ^= 1;
    }

    auto& v = voices[current];
    v = Voice {};
    v.active = true;
    v.velocity = sanitize (velocity, 0.0f, 1.0f, 1.0f);

    const double ts = std::min (1.0, std::max (0.3, (double) tailScale));
    const double msToS = sr * 0.001;

    v.startHz   = sanitize (s.startHz, 20.0f, 5000.0f, 480.0f);
    v.endHz     = sanitize (s.endHz, 15.0f, 400.0f, 46.25f);
    v.logRatio  = std::log (v.startHz / v.endHz);
    v.pitchLen  = std::max (1.0, (double) sanitize (s.pitchMs, 0.5f, 2000.0f, 45.0f) * msToS);
    v.pitchCurve = sanitize (s.pitchCurve, 0.1f, 12.0f, 2.5f);

    v.attackLen = std::max (1.0, (double) sanitize (s.attackMs, 0.0f, 50.0f, 0.3f) * msToS);
    v.holdEnd   = v.attackLen + (double) sanitize (s.holdMs, 0.0f, 1000.0f, 25.0f) * msToS;
    v.decayLen  = std::max (1.0, ts * (double) sanitize (s.decayMs, 5.0f, 4000.0f, 220.0f) * msToS);
    v.decayEnd  = v.holdEnd + v.decayLen;
    v.tailLen   = std::max (1.0, ts * (double) sanitize (s.tailMs, 0.0f, 4000.0f, 120.0f) * msToS);
    v.tailEnd   = v.decayEnd + v.tailLen;
    v.decayCurve = sanitize (s.decayCurve, 0.2f, 10.0f, 1.6f);
    v.tailLevel  = sanitize (s.tailLevel, 0.0f, 1.0f, 0.15f);

    v.phase = wrap01 ((double) sanitize (s.startPhaseDeg, 0.0f, 360.0f, 0.0f) / 360.0);

    v.clickGain = dbToGain (sanitize (s.clickLevelDb, -100.0f, 6.0f, -16.0f));
    const double clickTau = std::max (0.05, (double) sanitize (s.clickDecayMs, 0.1f, 100.0f, 4.0f)) * msToS;
    v.clickCoef = std::exp (-1.0 / clickTau);
    v.clickEnv  = 1.0;
    v.clickCoeffs.set (sanitize (s.clickToneHz, 200.0f, 18000.0f, 3500.0f), sr, 1.0);
    v.noise.seed (0x9E3779B9u);   // fester Seed: jeder Kick-Click ist identisch
    ++triggerCount;
}

float KickEngine::Voice::tick (double sampleRate) noexcept
{
    const double td = (double) t;

    // Pitch: log-Frequenz-Sweep
    double e = 0.0;
    if (td < pitchLen)
        e = std::pow (1.0 - td / pitchLen, pitchCurve);
    const double f = endHz * std::exp (logRatio * e);
    lastFreq = f;
    lastPitchExp = logRatio * e;

    // Amplitude
    double amp;
    if (td < attackLen)        amp = td / attackLen;
    else if (td < holdEnd)     amp = 1.0;
    else if (td < decayEnd)
    {
        const double x = (td - holdEnd) / decayLen;
        amp = tailLevel + (1.0 - tailLevel) * std::pow (1.0 - x, decayCurve);
    }
    else if (td < tailEnd)
    {
        const double x = (td - decayEnd) / tailLen;
        amp = tailLevel * (1.0 - x) * (1.0 - x);
    }
    else
    {
        active = false;
        return 0.0f;
    }

    lastAmp = amp;
    double out = amp * std::sin (kTwoPi * phase);

    // Click-Layer
    if (clickGain > 0.0 && clickEnv > 1.0e-5)
    {
        const auto o = svfTick (clickSvf, clickCoeffs, (double) noise.next());
        out += clickGain * clickEnv * o.bp * 2.0;
        clickEnv *= clickCoef;
    }

    phase += f / sampleRate;
    phase -= std::floor (phase);
    ++t;

    return (float) (out * velocity);
}

void KickEngine::render (float* out, int n, const KickSettings& live) noexcept
{
    drive.setTarget ((float) driveDbToFactor (sanitize (live.driveDb, 0.0f, 36.0f, 4.0f)));
    if (! primed) { drive.reset ((float) driveDbToFactor (sanitize (live.driveDb, 0.0f, 36.0f, 4.0f))); primed = true; }

    const bool smpActive = svoices[0].active || svoices[1].active;
    const float hz = sanitize (live.smpLowCutHz, 20.0f, 2000.0f, 20.0f);
    if (smpActive && std::abs (hz - smpHpHz) > 1.0e-3f) { smpHpHz = hz; smpHpC.set (hz, sr, 1.41421356); }
    const bool hpOn = hz > 20.5f;
    const double ampMod = sanitize (live.smpAmpEnv, 0.0f, 1.0f, 0.0f);
    const double pitchMod = sanitize (live.smpPitchEnv, 0.0f, 1.0f, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        double x = 0.0;
        for (int vi = 0; vi < 2; ++vi)
        {
            auto& v = voices[vi];
            if (! v.active) continue;
            double s = v.tick (sr);
            if (v.fadeStep > 0.0f)
            {
                s *= v.fade;
                v.fade -= v.fadeStep;
                if (v.fade <= 0.0f) { v.active = false; v.fade = 0.0f; }
            }
            x += s;
        }

        // Sample-Layer: optional von der Hüllkurve, dem Pitch-Verlauf und der Sättigung der Kick moduliert
        double smpOut = 0.0;
        if (smpActive)
        {
            const auto& mv = voices[current];
            const double envAmp = mv.active ? mv.lastAmp : 0.0;
            const float envGain = (float) (1.0 - ampMod + ampMod * envAmp);
            const double rateMul = pitchMod > 0.0 && mv.active ? std::exp (pitchMod * mv.lastPitchExp) : 1.0;
            for (auto& v : svoices)
                if (v.active) smpOut += v.tick (rateMul, envGain);
            if (hpOn)
            {
                smpOut = svfTick (smpHp1, smpHpC, smpOut).hp;
                smpOut = svfTick (smpHp2, smpHpC, smpOut).hp;
            }
        }

        if (live.smpDrive)
            out[i] = (float) shaper.process (x + smpOut, (double) drive.next());
        else
            out[i] = (float) (shaper.process (x, (double) drive.next()) + smpOut);
    }

    if (! smpActive)
    {
        smpHp1.reset();
        smpHp2.reset();
    }
}

} // namespace samsi
