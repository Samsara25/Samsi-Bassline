#include "AutoFit.h"

#include "../dsp/DspUtils.h"

#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace samsi
{

namespace
{
    std::string fmt (const char* f, double a, double b = 0.0, double c = 0.0)
    {
        char buf[256];
        std::snprintf (buf, sizeof buf, f, a, b, c);
        return buf;
    }

    struct Evaluator
    {
        const Settings& base;
        const PatternData& pattern;
        const Layers& layers;
        double sr, bpm;
        FitReference ref;
        const std::atomic<bool>& cancel;
        std::atomic<float>& progress;
        std::atomic<int> count { 0 };
        int expected = 200;

        double eval (const FitSettings& f, FitMetrics* out = nullptr)
        {
            Settings s = base;
            s.fit = f;
            s.fit.enabled = true;
            s.fit.duckLowOnly = base.fit.duckLowOnly;   // Nutzervorgaben
            s.fit.duckHz = base.fit.duckHz;
            const auto r = renderPattern (s, pattern, sr, bpm, layers);
            const auto m = analyze (r, ref);
            const int c = ++count;
            progress.store (std::min (0.99f, (float) c / (float) expected));
            if (out != nullptr) *out = m;
            return m.cost + changeCost (f, base.bass.startPhaseDeg);
        }

        /** Wertet Kandidaten parallel aus (jeder rendert mit eigener Engine).
            Auswahl deterministisch: niedrigste Kosten, bei Gleichstand der frühere Kandidat. */
        void batch (const std::vector<FitSettings>& cands, FitSettings& best, double& bestCost)
        {
            if (cands.empty() || cancel.load()) return;
            std::vector<double> costs (cands.size(), 1.0e300);
            std::atomic<size_t> next { 0 };
            auto worker = [&]
            {
                for (;;)
                {
                    const size_t i = next++;
                    if (i >= cands.size() || cancel.load()) return;
                    costs[i] = eval (cands[i]);
                }
            };
            const unsigned hw = std::max (1u, std::thread::hardware_concurrency());
            const unsigned nThreads = std::min<unsigned> ((unsigned) cands.size(), std::min (6u, std::max (1u, hw - 1)));
            std::vector<std::thread> pool;
            for (unsigned t = 1; t < nThreads; ++t) pool.emplace_back (worker);
            worker();
            for (auto& t : pool) t.join();

            for (size_t i = 0; i < cands.size(); ++i)
                if (costs[i] < bestCost - 1.0e-9) { bestCost = costs[i]; best = cands[i]; }
        }
    };

    template <typename Setter>
    void searchValues (Evaluator& ev, FitSettings& best, double& bestCost, std::initializer_list<float> values, Setter set)
    {
        std::vector<FitSettings> c;
        for (float v : values) { FitSettings f = best; set (f, v); c.push_back (f); }
        ev.batch (c, best, bestCost);
    }

    void refinePhase (Evaluator& ev, FitSettings& best, double& bestCost, float span, float step)
    {
        std::vector<FitSettings> c;
        for (float d = -span; d <= span + 1.0e-3f; d += step)
        {
            if (std::abs (d) < 1.0e-3f) continue;
            FitSettings f = best;
            f.phaseDeg = std::fmod (best.phaseDeg + d + 720.0f, 360.0f);
            c.push_back (f);
        }
        ev.batch (c, best, bestCost);
    }
}

FitResult runAutoFit (const Settings& sIn, const PatternData& pattern, double sampleRate, double bpm,
                      const std::atomic<bool>& cancel, std::atomic<float>& progress, const Layers& layers)
{
    FitResult res;
    progress.store (0.0f);

    Settings s = sIn;
    const auto& con = s.constraints;

    // 0. Kick-Sample an der Synth-Kick ausrichten (Zeitversatz + Polarität, maximale Tiefband-Korrelation)
    if (s.kick.smpOn && layers.kickSample != nullptr)
    {
        res.kickAlign = alignKickSample (s.kick, *layers.kickSample, sampleRate);
        if (res.kickAlign.valid)
        {
            s.kick.smpStartMs = res.kickAlign.startMs;
            s.kick.smpOffsetMs = res.kickAlign.delayMs;
            s.kick.smpInvert = res.kickAlign.invert;
        }
    }

    // Ausgangslage: Fit deaktiviert, Sample wie vom Nutzer eingestellt
    {
        Settings b = sIn;
        b.fit.enabled = false;
        const auto r = renderPattern (b, pattern, sampleRate, bpm, layers);
        res.before = analyze (r, {});
    }

    Evaluator ev { s, pattern, layers, sampleRate, bpm, {}, cancel, progress };
    ev.ref.bassEnergyDb = res.before.bassEnergyDb;
    ev.ref.noteSigmaDb  = res.before.noteSigmaDb;
    ev.ref.valid = true;

    const bool allowTiming = ! con.lockTiming && con.maxShiftMs > 0.01f;
    const float maxShift = std::min (5.0f, std::max (0.0f, con.maxShiftMs));
    ev.expected = 98 + 8 + (con.lockBassTone ? 0 : 25) + (allowTiming ? (int) (maxShift * 8) + 1 : 0)
                + (con.lockKick ? 0 : 4) + 8 + 8;

    // neutraler Startpunkt -> reproduzierbares Ergebnis unabhängig von alten Fit-Werten
    FitSettings best;
    best.enabled = true;
    best.duckLowOnly = s.fit.duckLowOnly;
    best.duckHz = s.fit.duckHz;
    best.duckMs = 80.0f;
    double bestCost = ev.eval (best);

    // 1. Phase x Polarität x Modus
    {
        std::vector<FitSettings> c;
        for (int mode = 0; mode < 2; ++mode)
            for (int pol = 0; pol < 2; ++pol)
                for (int deg = 0; deg < 360; deg += 15)
                {
                    FitSettings f = best;
                    f.phaseMode = mode == 0 ? PhaseMode::Fixed : PhaseMode::FollowKick;
                    f.polarity = pol == 1;
                    f.phaseDeg = (float) deg;
                    c.push_back (f);
                }
        ev.batch (c, best, bestCost);
    }
    refinePhase (ev, best, bestCost, 12.0f, 3.0f);

    // 2. Hüllkurvenabstimmung & Ducking
    if (! con.lockBassTone)
    {
        searchValues (ev, best, bestCost, { 0.5f, 1.0f, 2.0f, 3.5f, 5.0f },
                      [] (FitSettings& c, float v) { c.attackAddMs = v; });
        std::vector<FitSettings> c;
        for (float depth : { 2.0f, 4.0f, 6.0f, 9.0f, 12.0f })
            for (float ms : { 40.0f, 70.0f, 110.0f, 160.0f })
            {
                FitSettings f = best;
                f.duckDb = depth;
                f.duckMs = ms;
                c.push_back (f);
            }
        ev.batch (c, best, bestCost);
    }

    // 3. begrenzter Zeitversatz
    if (allowTiming)
    {
        std::vector<FitSettings> c;
        for (float o = -maxShift; o <= maxShift + 1.0e-3f; o += 0.25f)
        {
            if (std::abs (o) < 1.0e-3f) continue;
            FitSettings f = best;
            f.offsetMs = o;
            c.push_back (f);
        }
        ev.batch (c, best, bestCost);
    }

    // 4. Kick-Tail
    if (! con.lockKick)
        searchValues (ev, best, bestCost, { 90.0f, 80.0f, 70.0f, 60.0f },
                      [] (FitSettings& c, float v) { c.kickTailPct = v; });

    // 5. Pegelkorrektur (klar begrenzt)
    searchValues (ev, best, bestCost, { -1.0f, -0.5f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f },
                  [] (FitSettings& c, float v) { c.trimDb = v; });

    // 6. erneute Phasen-Feinsuche
    refinePhase (ev, best, bestCost, 6.0f, 1.5f);

    if (cancel.load())
    {
        progress.store (0.0f);
        return res;
    }

    ev.eval (best, &res.after);
    res.best = best;
    res.evaluations = ev.count.load();
    res.abGainDb = std::max (-12.0, std::min (12.0, res.after.loudnessLu - res.before.loudnessLu));
    res.ok = true;

    //--- verständliche Hinweise -----------------------------------------------
    auto& n = res.notes;
    const double subB = res.before.cancelMeanDb[0], subA = res.after.cancelMeanDb[0];
    n.push_back (fmt ("Sub-Auslöschung 20-60 Hz (Mittel): %.1f dB -> %.1f dB", subB, subA));
    if (subA > 3.0)
        n.push_back ("Restliche Sub-Auslöschung: Kick-Grundton und Bass laufen mit verschiedenen Frequenzen "
                     "durch die Phase. Eine einzelne Phasenlage kann das nicht überall verhindern - "
                     "Kick-Grundton/Tonart anpassen oder Ducking zulassen.");
    if (res.after.sumPeakDb > -1.0)
        n.push_back (fmt ("Summenpeak %.1f dBFS - Master oder Pegel absenken (kein versteckter Limiter).", res.after.sumPeakDb));
    if (best.duckDb > 0.0f)
        n.push_back (fmt ("Ducking %.0f dB / %.0f ms: weniger Überlappung, dafür etwas leiserer erster Bass nach der Kick.",
                          best.duckDb, best.duckMs));
    const double loss = res.before.bassEnergyDb - res.after.bassEnergyDb;
    if (loss > 0.3)
        n.push_back (fmt ("Bass-Energie 20-250 Hz: -%.1f dB gegenüber vorher (Grenze %.0f dB).", loss, 4.0));
    if (res.after.noteSigmaDb > res.before.noteSigmaDb + 0.5)
        n.push_back (fmt ("Bassnoten ungleichmäßiger: Streuung %.1f -> %.1f dB.", res.before.noteSigmaDb, res.after.noteSigmaDb));
    if (best.phaseMode == PhaseMode::FollowKick)
        n.push_back ("Phasenmodus 'Follow Kick': Startphase folgt kausal der laufenden Kick-Phase (auch für Live-MIDI).");
    if (std::abs (best.offsetMs) > 0.01f)
        n.push_back (fmt ("Bass-Versatz %.2f ms. Bei Live-MIDI werden nur positive Versätze angewendet.", best.offsetMs));
    if (res.kickAlign.valid)
        n.push_back (fmt ("Kick-Sample ausgerichtet: Tiefband-Korrelation zur Synth-Kick %.2f -> %.2f.",
                          res.kickAlign.coherenceBefore, res.kickAlign.coherenceAfter));
    if (best.polarity)
        n.push_back ("Polarität invertiert: wirkt auf das ganze Bass-Signal (nicht gleich einer 180-Grad-Startphase, "
                     "weil Saw/Drive asymmetrisch sind).");

    progress.store (1.0f);
    return res;
}

} // namespace samsi
