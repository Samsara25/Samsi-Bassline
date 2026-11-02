#include "Glue.h"

namespace samsi
{

namespace
{
    inline double coefFor (double ms, double sr) noexcept { return std::exp (-1.0 / std::max (1.0, ms * 0.001 * sr)); }
    inline double toDb (double x) noexcept { return 20.0 * std::log10 (std::max (x, 1.0e-9)); }
}

void KickBassGlue::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    detRel = coefFor (8.0, sr);
    reset();
}

void KickBassGlue::reset() noexcept
{
    scHpS.reset(); scLpS.reset();
    det = 0.0; grDb = 0.0; grLin = 1.0;
    for (auto& b : bands) { b.keyS.reset(); b.sL.reset(); b.sR.reset(); b.det = 0.0; b.gainDb = 0.0; }
    lastHp = lastLp = -1.0f;
    lastBandF[0] = lastBandF[1] = -1.0f;
    compGrDb = 0.0f; bandGainDb[0] = bandGainDb[1] = 0.0f; maxCompGrDb = 0.0f;
}

void KickBassGlue::update (const GlueSettings& g) noexcept
{
    const float hp = sanitize (g.scHpHz, 10.0f, 2000.0f, 20.0f), lp = sanitize (g.scLpHz, 40.0f, 20000.0f, 300.0f);
    if (hp != lastHp) { lastHp = hp; scHp.set (hp, sr, 1.41421356); }
    if (lp != lastLp) { lastLp = lp; scLp.set (lp, sr, 1.41421356); }
    const double pump = g.style == 1 ? 1.6 : 1.0;
    attC = coefFor (sanitize (g.attackMs, 0.02f, 200.0f, 1.0f) * pump, sr);
    relC = coefFor (sanitize (g.releaseMs, 2.0f, 2000.0f, 80.0f) * pump, sr);
    for (int i = 0; i < 2; ++i)
    {
        const auto& bs = g.band[i];
        auto& b = bands[i];
        const float f = sanitize (bs.freqHz, 20.0f, 2000.0f, 55.0f), q = sanitize (bs.q, 0.2f, 10.0f, 1.0f);
        if (f != lastBandF[i] || q != lastBandQ[i])
        {
            lastBandF[i] = f; lastBandQ[i] = q;
            b.keyC.set (f, sr, 1.0 / q);
            b.g = SvfCoeffs::prewarp (f, sr);
        }
        b.attC = coefFor (sanitize (bs.attackMs, 0.02f, 200.0f, 1.0f), sr);
        b.relC = coefFor (sanitize (bs.releaseMs, 2.0f, 2000.0f, 60.0f), sr);
    }
}

void KickBassGlue::process (float key, float& bl, float& br, const GlueSettings& g) noexcept
{
    //--- Sidechain-Kompressor --------------------------------------------------------------
    if (g.compOn)
    {
        double k = svfTick (scHpS, scHp, (double) key).hp;
        k = svfTick (scLpS, scLp, k).lp;
        const double a = std::abs (k);
        det = a > det ? a : det * detRel;                  // Spitzen-Detektor (sofort an, 8 ms ab)
        const double lvl = toDb (det);
        const double thr = sanitize (g.threshDb, -80.0f, 0.0f, -24.0f);
        const double ratio = sanitize (g.ratio, 1.0f, 100.0f, 3.0f);
        const double knee = sanitize (g.kneeDb, 0.0f, 36.0f, 6.0f);
        const double over = lvl - thr;
        double target = 0.0;   // gewünschte Reduktion in dB (>= 0)
        if (knee > 0.0 && std::abs (over) <= knee * 0.5)
            target = (1.0 - 1.0 / ratio) * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
        else if (over > 0.0)
            target = (1.0 - 1.0 / ratio) * over;
        target = std::min (target, (double) sanitize (g.rangeDb, 0.0f, 48.0f, 6.0f));
        if (g.style == 1)
        {
            // Pumping: lineare Verstärkung geglättet -> weichere, hörbare Rückkehr
            const double tl = std::pow (10.0, -target / 20.0);
            grLin = tl < grLin ? tl + (grLin - tl) * attC : tl + (grLin - tl) * relC;
            grDb = -toDb (grLin);
        }
        else
        {
            grDb = target > grDb ? target + (grDb - target) * attC : target + (grDb - target) * relC;
            grLin = std::pow (10.0, -grDb / 20.0);
        }
        const double m = sanitize (g.mix, 0.0f, 1.0f, 1.0f);
        const double gain = 1.0 - m + m * grLin;
        bl = (float) (bl * gain);
        br = (float) (br * gain);
        compGrDb = (float) -toDb (gain);
        maxCompGrDb = std::max (maxCompGrDb, compGrDb);
    }
    else
        compGrDb = 0.0f;

    //--- Dynamischer EQ ------------------------------------------------------------------
    for (int i = 0; i < 2; ++i)
    {
        const auto& bs = g.band[i];
        auto& b = bands[i];
        if (! bs.on) { bandGainDb[i] = 0.0f; continue; }
        // Schlüssel: Kick im selben Band (Bandpass, auf Spitzengewinn 1 normiert)
        const double kb = svfTick (b.keyS, b.keyC, (double) key).bp * b.keyC.k;
        const double a = std::abs (kb);
        b.det = a > b.det ? a : b.det * detRel;
        const double over = toDb (b.det) - (double) sanitize (bs.threshDb, -80.0f, 0.0f, -30.0f);
        const double range = sanitize (bs.rangeDb, -24.0f, 24.0f, -6.0f);
        const double amount = std::min (std::abs (range), std::max (0.0, over * 0.75));   // 4:1 oberhalb des Thresholds
        const double target = range < 0.0 ? -amount : amount;
        const bool towards = std::abs (target) > std::abs (b.gainDb);
        b.gainDb = target + (b.gainDb - target) * (towards ? b.attC : b.relC);
        bandGainDb[i] = (float) b.gainDb;

        // Filter mit aktueller Verstärkung (Simper-SVF: Glocke bzw. Low-Shelf)
        const double q = sanitize (bs.q, 0.2f, 10.0f, 1.0f);
        // Filterzustand immer mitführen; bei 0 dB sind m1 = m2 = 0 -> exakt durchlassen
        const double A = std::pow (10.0, b.gainDb / 40.0);
        SvfCoeffs c;
        double m1, m2;
        if (bs.type == 1)
        {
            c.setG (b.g / std::sqrt (A), 1.0 / q);
            m1 = c.k * (A - 1.0); m2 = A * A - 1.0;
        }
        else
        {
            c.setG (b.g, 1.0 / (q * A));
            m1 = c.k * (A * A - 1.0); m2 = 0.0;
        }
        const auto oL = svfTick (b.sL, c, (double) bl);
        const auto oR = svfTick (b.sR, c, (double) br);
        bl = (float) (bl + m1 * oL.bp + m2 * oL.lp);
        br = (float) (br + m1 * oR.bp + m2 * oR.lp);
    }
}

} // namespace samsi
