// Automatisierte Tests für Samsi Bassline.
// Betreibt den echten AudioProcessor ohne Host (eigener PlayHead, eigene MIDI-Puffer).
// Aufruf: SamsiTests [--screenshot pfad.png]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "fit/AutoFit.h"
#include "dsp/KickEngine.h"
#include "dsp/OutputUtility.h"
#include "analysis/LiveAnalyzer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <random>

using namespace samsi;

//==============================================================================
static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FEHLER: " __VA_ARGS__); std::printf ("   [%s:%d]\n", __FILE__, __LINE__); } } while (0)

static void section (const char* name) { std::printf ("\n== %s ==\n", name); }

//==============================================================================
struct TestPlayHead : juce::AudioPlayHead
{
    bool playing = true;
    double ppq = 0.0, bpm = 145.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Out
{
    std::vector<float> ch[6];   // 0/1 Mix, 2/3 Kick, 4/5 Bass
    size_t size() const { return ch[0].size(); }
};

using BlockFn = std::function<int (int blockIndex)>;
using MidiFn  = std::function<void (int64_t startSample, int n, juce::MidiBuffer&)>;
using TransportFn = std::function<void (int64_t startSample, TestPlayHead&)>;

static Out run (SamsiBasslineProcessor& p, double sr, int total, BlockFn blockFn, double bpm = 145.0,
                MidiFn midiFn = nullptr, TransportFn transportFn = nullptr, bool playing = true, int maxBlock = 512)
{
    TestPlayHead ph;
    ph.bpm = bpm;
    ph.playing = playing;
    p.setPlayHead (&ph);
    p.prepareToPlay (sr, maxBlock);

    Out o;
    for (auto& c : o.ch) c.reserve ((size_t) total);
    juce::AudioBuffer<float> buf (6, 8192);
    juce::MidiBuffer midi;
    int64_t pos = 0;
    int bi = 0;
    while (pos < total)
    {
        const int n = (int) std::min<int64_t> (blockFn (bi++), total - pos);
        ph.ppq = (double) pos / (60.0 / ph.bpm * sr);
        if (transportFn) transportFn (pos, ph);
        buf.setSize (6, n, false, false, true);
        midi.clear();
        if (midiFn) midiFn (pos, n, midi);
        p.processBlock (buf, midi);
        for (int c = 0; c < 6; ++c)
            o.ch[c].insert (o.ch[c].end(), buf.getReadPointer (c), buf.getReadPointer (c) + n);
        pos += n;
    }
    p.releaseResources();
    p.setPlayHead (nullptr);
    return o;
}

static double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}
static double peak (const std::vector<float>& a) { double m = 0; for (float v : a) m = std::max (m, (double) std::abs (v)); return m; }
static bool allFinite (const Out& o) { for (auto& c : o.ch) for (float v : c) if (! std::isfinite (v)) return false; return true; }
static double rms (const std::vector<float>& a, size_t from, size_t to)
{
    double s = 0; to = std::min (to, a.size());
    for (size_t i = from; i < to; ++i) s += (double) a[i] * a[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

static void setParam (SamsiBasslineProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static BlockFn fixedBlock (int n) { return [n] (int) { return n; }; }

//==============================================================================
static void testNullTest()
{
    section ("Nulltest MIX = KICK + BASS (Bus-Clipper aus)");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        SamsiBasslineProcessor p;
        setParam (p, pid::bWidth, 0.7f);       // Stereo-Breite aktiv
        setParam (p, pid::fDuckDb, 6.0f);      // Fit-Ducking aktiv
        setParam (p, pid::fPolarity, 1.0f);
        const auto o = run (p, sr, (int) (sr * 3.0), fixedBlock (512));
        double dl = 0, dr = 0;
        for (size_t i = 0; i < o.size(); ++i)
        {
            dl = std::max (dl, (double) std::abs (o.ch[0][i] - (o.ch[2][i] + o.ch[4][i])));
            dr = std::max (dr, (double) std::abs (o.ch[1][i] - (o.ch[3][i] + o.ch[5][i])));
        }
        std::printf ("  %6.0f Hz: max |MIX-(KICK+BASS)| L=%.3g R=%.3g, Peak MIX %.2f dBFS\n", sr, dl, dr,
                     juce::Decibels::gainToDecibels ((float) peak (o.ch[0])));
        CHECK (dl == 0.0 && dr == 0.0, "Nulltest nicht exakt bei %.0f Hz", sr);
        CHECK (peak (o.ch[2]) > 0.05 && peak (o.ch[4]) > 0.02, "Kick oder Bass stumm");
    }
}

static void testBlockSizes()
{
    section ("Variable Blockgrößen – identisches Ergebnis");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const int total = (int) (sr * 2.5);
        SamsiBasslineProcessor ref;
        const auto a = run (ref, sr, total, fixedBlock (512));

        std::mt19937 rng (1234);
        std::uniform_int_distribution<int> dist (1, 2048);
        const std::vector<std::pair<const char*, BlockFn>> variants {
            { "32",          fixedBlock (32) },
            { "441",         fixedBlock (441) },
            { "1024",        fixedBlock (1024) },
            { "4096 (>max)", fixedBlock (4096) },
            { "zufaellig 1..2048", [&] (int) { return dist (rng); } } };

        for (auto& v : variants)
        {
            SamsiBasslineProcessor p;
            const auto b = run (p, sr, total, v.second);
            double d = 0;
            for (int c = 0; c < 6; ++c) d = std::max (d, maxAbsDiff (a.ch[c], b.ch[c]));
            std::printf ("  %6.0f Hz, Block %-18s max. Abweichung %.3g\n", sr, v.first, d);
            CHECK (d < 1.0e-4, "Blockgroesse %s weicht ab (%.3g)", v.first, d);
            CHECK (allFinite (b), "NaN/Inf bei Blockgroesse %s", v.first);
        }
    }
}

static void testTriggerCounts()
{
    section ("Samplegenaue Trigger, Loop-Sprünge, Tempowechsel");
    // direkt mit der Engine, um ausgelöste Ereignisse zu zählen
    auto countKicks = [] (double sr, double bpm, std::function<int (int)> blk, std::function<double (int64_t, double)> ppqFn,
                          int total, std::vector<int64_t>* outPos = nullptr)
    {
        SamsiEngine e;
        e.prepare (sr, 512);
        TriggerLog log;
        log.kickOn.reserve (4096); log.bassOn.reserve (8192); log.bassOff.reserve (8192);
        e.setTriggerLog (&log);
        Settings s;
        const auto pat = PatternData::makeDefault();
        int64_t pos = 0; int bi = 0;
        while (pos < total)
        {
            int n = std::min (blk (bi++), (int) (total - pos));
            n = std::min (n, 512);
            TransportInfo t; t.playing = true; t.hasPpq = true; t.bpm = bpm;
            t.ppq = ppqFn (pos, bpm);
            e.process (s, pat, t, nullptr, 0, n);
            pos += n;
        }
        if (outPos) *outPos = log.kickOn;
        return std::make_pair (log.kickOn.size(), log.bassOn.size());
    };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const double bpm = 145.0;
        const double spq = 60.0 / bpm * sr;
        const int total = (int) std::lround (spq * 16.0);   // 4 Takte
        std::mt19937 rng (7);
        std::uniform_int_distribution<int> dist (1, 700);
        std::vector<int64_t> pos;
        auto c = countKicks (sr, bpm, [&] (int) { return dist (rng); }, [spq] (int64_t p, double) { return (double) p / spq; }, total, &pos);
        std::printf ("  %6.0f Hz: %zu Kicks, %zu Bassnoten in 4 Takten (erwartet 16 / 48)\n", sr, c.first, c.second);
        CHECK (c.first == 16 && c.second == 48, "Triggeranzahl falsch");
        bool exact = true;
        for (size_t i = 0; i < pos.size(); ++i)
        {
            const double expected = (double) i * spq;
            if (std::abs ((double) pos[i] - expected) > 1.0) exact = false;
        }
        CHECK (exact, "Kick-Positionen nicht samplegenau");
    }

    // Loop: 1 Takt wird 3x wiederholt (Host springt von ppq 4 auf 0)
    {
        const double sr = 48000.0, bpm = 145.0, spq = 60.0 / bpm * sr;
        const int total = (int) std::lround (spq * 12.0);
        auto c = countKicks (sr, bpm, [] (int) { return 333; },
                             [spq] (int64_t p, double) { return std::fmod ((double) p / spq, 4.0); }, total);
        std::printf ("  Loop 1 Takt x3 (Host springt von 4 auf 0): %zu Kicks (erwartet 12)\n", c.first);
        CHECK (c.first == 12, "Loop-Sprung erzeugt falsche Kickanzahl");
    }

    // Tempowechsel 145 -> 150 BPM nach 2 Takten (Position kontinuierlich weitergeführt)
    {
        SamsiEngine e;
        e.prepare (48000.0, 512);
        TriggerLog log; log.kickOn.reserve (256); log.bassOn.reserve (512); log.bassOff.reserve (512);
        e.setTriggerLog (&log);
        Settings s;
        const auto pat = PatternData::makeDefault();
        double ppq = 0.0;
        while (ppq < 16.0 - 1.0e-9)
        {
            const double bpm = ppq < 8.0 ? 145.0 : 150.0;
            const double spq = 60.0 / bpm * 48000.0;
            // Blöcke enden genau an der Taktgrenze 8 und bei 16 (wie ein Host, der dort das Tempo wechselt)
            const double limit = ppq < 8.0 ? 8.0 : 16.0;
            const int n = (int) std::min (400.0, std::ceil ((limit - ppq) * spq - 1.0e-9));
            TransportInfo t; t.playing = true; t.hasPpq = true; t.bpm = bpm; t.ppq = ppq;
            e.process (s, pat, t, nullptr, 0, n);
            ppq += n / spq;
        }
        std::printf ("  Tempowechsel: %zu Kicks in 4 Takten (erwartet 16)\n", log.kickOn.size());
        CHECK (log.kickOn.size() == 16, "Tempowechsel: falsche Kickanzahl");
    }
}

static void testTransportStop()
{
    section ("Transport-Stopp: keine hängenden Noten");
    SamsiBasslineProcessor p;
    const double sr = 48000.0;
    // 1 s spielen, dann stoppen
    const auto o = run (p, sr, (int) (sr * 2.0), fixedBlock (256), 145.0, nullptr,
                        [sr] (int64_t pos, TestPlayHead& ph) { ph.playing = pos < (int64_t) sr; });
    const double tailRms = rms (o.ch[4], (size_t) (sr * 1.6), (size_t) (sr * 2.0));
    std::printf ("  Bass-RMS 0,6 s nach Stopp: %.3g\n", tailRms);
    CHECK (tailRms < 1.0e-4, "Bass klingt nach Stopp weiter");
    CHECK (allFinite (o), "NaN/Inf");
}

static void testMidi()
{
    section ("Externe MIDI-Steuerung");
    SamsiBasslineProcessor p;
    setParam (p, pid::seqOn, 0.0f);
    const double sr = 48000.0;
    // Kick auf C1 (36) bei 0, Bass E1 (40) bei 0,1 s bis 0,4 s, gebunden zu G1 (43) bei 0,3 s
    auto midi = [sr] (int64_t start, int n, juce::MidiBuffer& m)
    {
        auto put = [&] (int64_t at, juce::MidiMessage msg) { if (at >= start && at < start + n) m.addEvent (msg, (int) (at - start)); };
        put (0, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120));
        put ((int64_t) (0.1 * sr), juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
        put ((int64_t) (0.3 * sr), juce::MidiMessage::noteOn (1, 43, (juce::uint8) 100));
        put ((int64_t) (0.35 * sr), juce::MidiMessage::noteOff (1, 40));
        put ((int64_t) (0.5 * sr), juce::MidiMessage::noteOff (1, 43));
    };
    const auto o = run (p, sr, (int) (sr * 1.0), fixedBlock (512), 145.0, midi, nullptr, false);
    const double kick0 = rms (o.ch[2], 0, (size_t) (0.05 * sr));
    const double bassBefore = rms (o.ch[4], 0, (size_t) (0.095 * sr));
    const double bassOn = rms (o.ch[4], (size_t) (0.12 * sr), (size_t) (0.29 * sr));
    const double bassLegato = rms (o.ch[4], (size_t) (0.36 * sr), (size_t) (0.49 * sr));
    const double bassAfter = rms (o.ch[4], (size_t) (0.7 * sr), (size_t) (1.0 * sr));
    std::printf ("  Kick-RMS %.3f | Bass vorher %.2g, Note %.3f, gebunden %.3f, nach Note-Off %.2g\n",
                 kick0, bassBefore, bassOn, bassLegato, bassAfter);
    CHECK (kick0 > 0.05, "MIDI-Kick fehlt");
    CHECK (bassBefore < 1.0e-6, "Bass klingt vor seiner Note");
    CHECK (bassOn > 0.01 && bassLegato > 0.005, "MIDI-Bass fehlt");
    CHECK (bassAfter < 1.0e-4, "Bass haengt nach Note-Off");
}

static void testStateRecall()
{
    section ("Zustand speichern / laden");
    SamsiBasslineProcessor a;
    setParam (a, pid::kEnd, 52.0f);
    setParam (a, pid::bCutoff, 1500.0f);
    setParam (a, pid::fPhase, 123.0f);
    setParam (a, pid::fPhaseMode, 1.0f);
    setParam (a, pid::lockTiming, 0.0f);
    setParam (a, pid::swing, 0.2f);
    Step st; st.bass = true; st.note = 7; st.lenPct = 150; st.slide = true; st.bassVel = 90;
    a.setStep (5, st);

    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    SamsiBasslineProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());

    int mismatched = 0;
    for (auto* prm : a.getParameters())
    {
        auto* pa = dynamic_cast<juce::RangedAudioParameter*> (prm);
        auto* pb = dynamic_cast<juce::RangedAudioParameter*> (b.apvts.getParameter (pa->getParameterID()));
        if (pb == nullptr || std::abs (pa->getValue() - pb->getValue()) > 1.0e-6f) ++mismatched;
    }
    const auto s2 = b.pattern.getStep (5);
    std::printf ("  %d Parameter, Abweichungen: %d; Step 6: note %+d len %d%% slide %d vel %d\n",
                 a.getParameters().size(), mismatched, s2.note, s2.lenPct, (int) s2.slide, s2.bassVel);
    CHECK (mismatched == 0, "Parameter nach Laden verschieden");
    CHECK (s2.note == 7 && s2.lenPct == 150 && s2.slide && s2.bassVel == 90 && s2.bass, "Pattern nicht wiederhergestellt");

    const auto oa = run (a, 48000.0, 96000, fixedBlock (512));
    const auto ob = run (b, 48000.0, 96000, fixedBlock (512));
    double d = 0; for (int c = 0; c < 6; ++c) d = std::max (d, maxAbsDiff (oa.ch[c], ob.ch[c]));
    std::printf ("  Audio nach Wiederherstellung: max. Abweichung %.3g\n", d);
    CHECK (d == 0.0, "Audio nach Laden verschieden");

    // ungültige Daten dürfen nichts kaputt machen
    const char junk[] = "keine gueltigen Plugin-Daten";
    b.setStateInformation (junk, (int) sizeof junk);
    CHECK (std::abs (b.apvts.getRawParameterValue (pid::kEnd)->load() - 52.0f) < 0.01f, "Ungueltiger Zustand hat Werte veraendert");
}

static void testInstances()
{
    section ("Mehrere Instanzen unabhängig");
    SamsiBasslineProcessor solo;
    setParam (solo, pid::bCutoff, 400.0f);
    const auto ref = run (solo, 48000.0, 48000, fixedBlock (256));

    SamsiBasslineProcessor a, b;
    setParam (a, pid::bCutoff, 400.0f);
    setParam (b, pid::bCutoff, 5000.0f);
    setParam (b, pid::kEnd, 60.0f);
    // abwechselnd blockweise verarbeiten
    TestPlayHead pha, phb;
    a.setPlayHead (&pha); b.setPlayHead (&phb);
    a.prepareToPlay (48000.0, 256); b.prepareToPlay (48000.0, 256);
    std::vector<float> outA;
    juce::AudioBuffer<float> ba (6, 256), bb (6, 256);
    juce::MidiBuffer m;
    for (int pos = 0; pos < 48000; pos += 256)
    {
        pha.ppq = phb.ppq = pos / (60.0 / 145.0 * 48000.0);
        a.processBlock (ba, m);
        b.processBlock (bb, m);
        outA.insert (outA.end(), ba.getReadPointer (0), ba.getReadPointer (0) + 256);
    }
    a.setPlayHead (nullptr); b.setPlayHead (nullptr);
    const double d = maxAbsDiff (ref.ch[0], outA);
    std::printf ("  Instanz A mit/ohne parallele Instanz B: max. Abweichung %.3g\n", d);
    CHECK (d == 0.0, "Instanzen beeinflussen sich");
}

static void testRealtimeVsOffline()
{
    section ("Echtzeit- vs. Offline-Rendering");
    SamsiBasslineProcessor rt, off;
    off.setNonRealtime (true);
    const auto a = run (rt, 48000.0, 96000, fixedBlock (512));
    const auto b = run (off, 48000.0, 96000, fixedBlock (512));
    double d = 0; for (int c = 0; c < 6; ++c) d = std::max (d, maxAbsDiff (a.ch[c], b.ch[c]));
    std::printf ("  max. Abweichung %.3g\n", d);
    CHECK (d == 0.0, "Offline-Rendering weicht ab");
}

static void testExtremes()
{
    section ("Extreme Einstellungen / ungültige Werte");
    std::mt19937 rng (99);
    int worstNan = 0;
    double worstPeak = 0;
    for (int trial = 0; trial < 40; ++trial)
    {
        SamsiBasslineProcessor p;
        for (auto* prm : p.getParameters())
        {
            const int mode = (int) (rng() % 3);
            prm->setValueNotifyingHost (mode == 0 ? 0.0f : mode == 1 ? 1.0f : (float) (rng() % 1000) / 999.0f);
        }
        setParam (p, pid::seqOn, 1.0f);
        setParam (p, pid::master, 0.0f);
        const auto o = run (p, trial % 2 ? 44100.0 : 96000.0, 30000, fixedBlock (trial % 3 == 0 ? 64 : 777));
        if (! allFinite (o)) ++worstNan;
        for (auto& c : o.ch) worstPeak = std::max (worstPeak, peak (c));
    }
    std::printf ("  40 Zufallskonfigurationen (Extremwerte): NaN/Inf-Laeufe %d, hoechster Peak %.2f dBFS\n",
                 worstNan, juce::Decibels::gainToDecibels ((float) worstPeak));
    CHECK (worstNan == 0, "NaN/Inf in Extremeinstellungen");
    CHECK (worstPeak < 16.0, "Extrem hoher Pegel (%.1f)", worstPeak);

    // Stille: Sequencer aus, kein MIDI -> exakt 0
    SamsiBasslineProcessor s;
    setParam (s, pid::seqOn, 0.0f);
    const auto o = run (s, 48000.0, 48000, fixedBlock (512));
    double pk = 0; for (auto& c : o.ch) pk = std::max (pk, peak (c));
    std::printf ("  Stille: Peak %.3g\n", pk);
    CHECK (pk == 0.0, "Stille erzeugt Signal");
}

static void testSmoothing()
{
    section ("Parameter-Glättung (keine Pegelsprünge)");
    // gehaltene MIDI-Bassnote, Master springt zwischen zwei Blöcken von 0 auf -24 dB
    auto render = [] (bool jump)
    {
        SamsiBasslineProcessor p;
        setParam (p, pid::seqOn, 0.0f);
        setParam (p, pid::bSus, 1.0f);
        setParam (p, pid::bDec, 2000.0f);
        auto midi = [] (int64_t start, int, juce::MidiBuffer& m) { if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 110), 0); };
        TestPlayHead ph; ph.playing = false;
        p.setPlayHead (&ph);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (6, 256);
        std::vector<float> out;
        juce::MidiBuffer m;
        for (int pos = 0; pos < 48000; pos += 256)
        {
            if (jump && pos == 24064) setParam (p, pid::master, -24.0f);
            m.clear(); midi (pos, 256, m);
            p.processBlock (buf, m);
            out.insert (out.end(), buf.getReadPointer (4), buf.getReadPointer (4) + 256);
        }
        p.setPlayHead (nullptr);
        return out;
    };
    const auto ref = render (false), jmp = render (true);
    // Verhältnis der Hüllkurven in 1-ms-Fenstern um den Sprung
    double maxStepDb = 0.0, prevDb = 0.0;
    for (int w = 0; w < 80; ++w)
    {
        const size_t a = 24064 - 960 + (size_t) w * 48;
        const double r1 = rms (ref, a, a + 48), r2 = rms (jmp, a, a + 48);
        const double db = 20.0 * std::log10 (std::max (1.0e-9, r2) / std::max (1.0e-9, r1));
        if (w > 0) maxStepDb = std::max (maxStepDb, std::abs (db - prevDb));
        prevDb = db;
    }
    std::printf ("  Master 0 -> -24 dB: groesster Pegelschritt je 1 ms = %.2f dB (Rampe 25 ms)\n", maxStepDb);
    CHECK (maxStepDb < 2.0, "Pegelsprung zu abrupt");
}

static void testAutoFit()
{
    section ("Auto Fit – Messung an ungünstigen Ausgangslagen");
    struct Case { const char* name; std::function<void (Settings&)> setup; };
    const std::vector<Case> cases {
        { "Standard (Grundton = Bassgrundton)", [] (Settings&) {} },
        { "Bass-Startphase 180 Grad, langer Kick-Tail", [] (Settings& s) { s.bass.startPhaseDeg = 180.0f; s.kick.tailMs = 400.0f; s.kick.tailLevel = 0.35f; } },
        { "Nur Sinus-Sub, Kick 44 Hz, Bass G1", [] (Settings& s) { s.bass.saw = 0.0f; s.bass.sub = 1.0f; s.kick.endHz = 44.0f; s.global.rootNote = 31; s.bass.startPhaseDeg = 200.0f; } },
        { "Timing frei (±2 ms), Kick frei", [] (Settings& s) { s.constraints.lockTiming = false; s.constraints.maxShiftMs = 2.0f; s.constraints.lockKick = false; s.bass.startPhaseDeg = 150.0f; } },
    };

    for (auto& c : cases)
    {
        Settings s;
        c.setup (s);
        const auto pat = PatternData::makeDefault();
        std::atomic<bool> cancel { false };
        std::atomic<float> progress { 0.0f };
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = runAutoFit (s, pat, 48000.0, 145.0, cancel, progress);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

        std::printf ("\n  Fall: %s  (%d Kandidaten, %.1f s)\n", c.name, r.evaluations, ms / 1000.0);
        std::printf ("    Ergebnis: Phase %.1f Grad, Modus %s, Polaritaet %s, Versatz %.2f ms, Ducking %.1f dB/%.0f ms, Attack+ %.2f ms, Trim %+.2f dB, Kick-Tail %.0f %%\n",
                     r.best.phaseDeg, r.best.phaseMode == PhaseMode::FollowKick ? "Follow Kick" : "Fixed", r.best.polarity ? "invertiert" : "normal",
                     r.best.offsetMs, r.best.duckDb, r.best.duckMs, r.best.attackAddMs, r.best.trimDb, r.best.kickTailPct);
        std::printf ("    %-28s %9s %9s\n", "Messwert", "vorher", "nachher");
        auto row = [] (const char* n, double a, double b) { std::printf ("    %-28s %9.2f %9.2f\n", n, a, b); };
        row ("Ausloeschung 20-60 Hz Mittel", r.before.cancelMeanDb[0], r.after.cancelMeanDb[0]);
        row ("Ausloeschung 20-60 Hz max", r.before.cancelWorstDb[0], r.after.cancelWorstDb[0]);
        row ("Ausloeschung 60-120 Hz Mittel", r.before.cancelMeanDb[1], r.after.cancelMeanDb[1]);
        row ("Einsatz-Ausloeschung dB", r.before.onsetCancelDb, r.after.onsetCancelDb);
        row ("Kick-Attack-Verlust dB", r.before.kickAttackLossDb, r.after.kickAttackLossDb);
        row ("Summenpeak dBFS", r.before.sumPeakDb, r.after.sumPeakDb);
        row ("Bass-Energie dB", r.before.bassEnergyDb, r.after.bassEnergyDb);
        row ("Lautheit K (LU)", r.before.loudnessLu, r.after.loudnessLu);
        row ("Zielfunktion", r.before.cost, r.after.cost);

        CHECK (r.ok, "Auto Fit ohne Ergebnis");
        CHECK (r.after.cost <= r.before.cost + 1.0e-6, "Auto Fit verschlechtert die Zielfunktion");
        CHECK (r.before.bassEnergyDb - r.after.bassEnergyDb < 4.0, "Bass wurde weggeduckt");
        CHECK (r.after.kickAttackLossDb <= r.before.kickAttackLossDb + 0.5, "Kick-Attack verschlechtert");
        if (s.constraints.lockTiming) CHECK (r.best.offsetMs == 0.0f, "Timing trotz Sperre veraendert");
        if (s.constraints.lockKick)   CHECK (r.best.kickTailPct == 100.0f, "Kick trotz Sperre veraendert");
    }

    // Reproduzierbarkeit
    {
        Settings s; s.bass.startPhaseDeg = 90.0f;
        std::atomic<bool> cancel { false }; std::atomic<float> pr { 0.0f };
        const auto r1 = runAutoFit (s, PatternData::makeDefault(), 48000.0, 145.0, cancel, pr);
        const auto r2 = runAutoFit (s, PatternData::makeDefault(), 48000.0, 145.0, cancel, pr);
        const bool same = r1.best.phaseDeg == r2.best.phaseDeg && r1.best.duckDb == r2.best.duckDb && r1.after.cost == r2.after.cost;
        std::printf ("\n  Reproduzierbarkeit (zweimal gleiche Eingabe): %s\n", same ? "identisch" : "VERSCHIEDEN");
        CHECK (same, "Auto Fit nicht reproduzierbar");
    }
}

static void testFitWorkflow()
{
    section ("Auto Fit im Plugin: Übernahme, Invalidierung, Undo");
    SamsiBasslineProcessor p;
    p.prepareToPlay (48000.0, 512);
    setParam (p, pid::bPhase, 170.0f);
    p.startAutoFit();
    const auto t0 = juce::Time::getMillisecondCounter();
    bool got = false;
    while (juce::Time::getMillisecondCounter() - t0 < 120000)
    {
        juce::Thread::sleep (50);
        if (p.pollFitResult()) { got = true; break; }
    }
    CHECK (got, "Kein Fit-Ergebnis");
    const float phaseAfter = p.apvts.getRawParameterValue (pid::fPhase)->load();
    std::printf ("  Fit uebernommen: Bass-Phase-Parameter = %.1f Grad, veraltet = %s\n", phaseAfter, p.isFitStale() ? "ja" : "nein");
    CHECK (! p.isFitStale(), "Frische Analyse als veraltet markiert");
    setParam (p, pid::bCutoff, 3000.0f);
    std::printf ("  nach Cutoff-Aenderung: veraltet = %s\n", p.isFitStale() ? "ja" : "nein");
    CHECK (p.isFitStale(), "Klangänderung invalidiert die Analyse nicht");
    CHECK (p.undoFit(), "Undo nicht moeglich");
    std::printf ("  nach Undo: Bass-Phase-Parameter = %.1f Grad\n", p.apvts.getRawParameterValue (pid::fPhase)->load());
    CHECK (p.apvts.getRawParameterValue (pid::fPhase)->load() == 0.0f, "Undo stellt nicht wieder her");
}

static void testAutomationParameterCount()
{
    section ("Parameter (automatisierbar, stabile IDs)");
    SamsiBasslineProcessor p;
    int automatable = 0;
    juce::StringArray ids;
    for (auto* prm : p.getParameters())
    {
        if (prm->isAutomatable()) ++automatable;
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) ids.add (r->getParameterID());
    }
    juce::StringArray unique (ids); unique.removeDuplicates (false);
    std::printf ("  %d Parameter, davon automatisierbar: %d, doppelte IDs: %d\n", p.getParameters().size(), automatable, ids.size() - unique.size());
    // Ausnahmen: Latenz-Schalter (MIDI-Lookahead, Time Shift ein, Limiter ein) und die Mess-Wahl Pre/Post
    CHECK (automatable == p.getParameters().size() - 4, "Unerwartete Anzahl automatisierbarer Parameter");
    for (auto* id : { pid::uTsOn, pid::uLimOn, pid::uPost })
        CHECK (! p.apvts.getParameter (id)->isAutomatable(), "%s darf nicht automatisierbar sein", id);
    CHECK (! p.apvts.getParameter (pid::lookahead)->isAutomatable(), "Lookahead darf nicht automatisierbar sein");
    CHECK (ids.size() == unique.size(), "Doppelte Parameter-IDs");

    // Busse
    std::printf ("  Ausgangsbusse: ");
    for (int i = 0; i < p.getBusCount (false); ++i)
        std::printf ("%s(%d) ", p.getBus (false, i)->getName().toRawUTF8(), p.getBus (false, i)->getNumberOfChannels());
    std::printf ("\n");
    CHECK (p.getBusCount (false) == 3, "Nicht drei Ausgangsbusse");
}


//==============================================================================
// Neue Funktionen: Sample-Layer, Wavetables, Pick, Tonart, Presets, Gate, Lookahead
//==============================================================================
#include "content/KeyAnalysis.h"
#include "content/PitchDetect.h"
#include "content/Presets.h"
#include "content/SampleLoader.h"
#include "content/WavetableFactory.h"

static std::vector<float> makeTestKick (double rate, double fundamental, double seconds = 0.5)
{
    std::vector<float> k ((size_t) (rate * seconds));
    double ph = 0.0;
    for (size_t i = 0; i < k.size(); ++i)
    {
        const double t = (double) i / rate;
        const double f = fundamental + 300.0 * std::exp (-t / 0.02);
        ph += 2.0 * 3.14159265358979 * f / rate;
        k[i] = (float) (0.9 * std::exp (-t / 0.25) * std::sin (ph));
    }
    return k;
}

static double energyAbove (const std::vector<float>& x, size_t from, size_t to, double rate, double hz)
{
    // einfacher Hochpass (2 x einpolig) und Energie
    const double a = std::exp (-2.0 * 3.14159265358979 * hz / rate);
    double lp1 = 0, lp2 = 0, e = 0;
    for (size_t i = 0; i < std::min (to, x.size()); ++i)
    {
        lp1 = a * lp1 + (1 - a) * x[i];
        lp2 = a * lp2 + (1 - a) * lp1;
        if (i >= from) { const double h = x[i] - lp2; e += h * h; }
    }
    return e;
}

static void testKickSampleLayer()
{
    section ("Kick-Sample-Layer");
    const auto wav = encodeWav (makeTestKick (44100.0, 55.0), 44100.0);

    SamsiBasslineProcessor p;
    const auto err = p.loadKickSampleFromMemory (wav, "testkick.wav");
    CHECK (err.isEmpty(), "Sample nicht geladen: %s", err.toRawUTF8());
    auto ks = p.getKickSample();
    CHECK (ks != nullptr, "kein Sample");
    if (ks == nullptr) return;
    std::printf ("  Sample: %zu Samples @ %.0f Hz, erkannter Grundton %.2f Hz (Konfidenz %.2f), erwartet ~55 Hz\n",
                 ks->data.size(), ks->rate, ks->pitchHz, ks->pitchConfidence);
    CHECK (std::abs (ks->pitchHz - 55.0f) < 2.0f, "Grundton-Erkennung ungenau");

    const auto off = run (p, 48000.0, 48000, fixedBlock (512));
    setParam (p, pid::ksOn, 1.0f);
    setParam (p, pid::ksLevel, 0.0f);
    setParam (p, pid::ksLowCut, 20.0f);
    const auto on = run (p, 48000.0, 48000, fixedBlock (512));
    const double eOff = rms (off.ch[2], 0, 48000), eOn = rms (on.ch[2], 0, 48000);
    std::printf ("  Kick-RMS ohne/mit Sample: %.4f / %.4f\n", eOff, eOn);
    CHECK (eOn > eOff * 1.1, "Sample-Layer hat keine Wirkung");

    double dl = 0;
    for (size_t i = 0; i < on.size(); ++i) dl = std::max (dl, (double) std::abs (on.ch[0][i] - (on.ch[2][i] + on.ch[4][i])));
    std::printf ("  Nulltest mit Sample: max. Abweichung %.3g\n", dl);
    CHECK (dl == 0.0, "Nulltest mit Sample verletzt");

    // Timing: Verzögerung 10 ms -> Einsatz 480 Samples später (Engine direkt, nur Sample hörbar)
    auto onset = [&] (float delayMs)
    {
        Settings s; s.kick.smpOn = true; s.kick.smpOffsetMs = delayMs; s.kick.smpLevelDb = 0.0f;
        Settings s0 = s; s0.kick.smpOn = false;
        Layers L; L.kickSample = ks.get();
        auto render = [&] (const Settings& st)
        {
            SamsiEngine e; e.prepare (48000.0, 512);
            TransportInfo t; t.playing = true; t.hasPpq = true; t.bpm = 145.0; t.ppq = 0.0;
            e.process (st, PatternData::makeDefault(), t, nullptr, 0, 512, L);
            t.ppq = 512.0 / (60.0 / 145.0 * 48000.0);
            std::vector<float> out (e.kick(), e.kick() + 512);
            e.process (st, PatternData::makeDefault(), t, nullptr, 0, 512, L);
            out.insert (out.end(), e.kick(), e.kick() + 512);
            return out;
        };
        const auto a = render (s), b = render (s0);
        for (size_t i = 0; i < a.size(); ++i) if (std::abs (a[i] - b[i]) > 1.0e-5f) return (int) i;
        return -1;
    };
    const int o0 = onset (0.0f), o10 = onset (10.0f);
    std::printf ("  Sample-Einsatz: ohne Verzögerung bei %d, mit 10 ms bei %d (Differenz %d, erwartet 480)\n", o0, o10, o10 - o0);
    CHECK (o0 >= 0 && std::abs ((o10 - o0) - 480) <= 1, "Sample-Verzögerung ungenau");

    // Zustand: Sample im Projekt eingebettet
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    SamsiBasslineProcessor q;
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    const auto a = run (p, 48000.0, 48000, fixedBlock (256));
    const auto b = run (q, 48000.0, 48000, fixedBlock (256));
    double d = 0; for (int c = 0; c < 6; ++c) d = std::max (d, maxAbsDiff (a.ch[c], b.ch[c]));
    std::printf ("  Projekt speichern/laden mit eingebettetem Sample (%.1f kB Zustand): Abweichung %.3g, Name '%s'\n",
                 mb.getSize() / 1024.0, d, q.getKickSample() ? q.getKickSample()->name.c_str() : "-");
    CHECK (q.getKickSample() != nullptr && d == 0.0, "Sample nicht im Zustand wiederhergestellt");
}

static void testLayerSwapWhilePlaying()
{
    section ("Sample/Wavetable austauschen während der Wiedergabe");
    SamsiBasslineProcessor p;
    setParam (p, pid::ksOn, 1.0f);
    setParam (p, pid::wtLevel, 0.8f);
    setParam (p, pid::wtTable, (float) kUserTableSlot);
    TestPlayHead ph;
    p.setPlayHead (&ph);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buf (6, 256);
    juce::MidiBuffer m;
    bool finite = true;
    std::vector<float> saw (48000);
    for (size_t i = 0; i < saw.size(); ++i) saw[i] = (float) (1.0 - 2.0 * std::fmod ((double) i * 110.0 / 48000.0, 1.0));
    for (int blk = 0; blk < 400; ++blk)
    {
        ph.ppq = blk * 256 / (60.0 / 145.0 * 48000.0);
        if (blk % 20 == 5)
            p.loadKickSampleFromMemory (encodeWav (makeTestKick (48000.0, 45.0 + blk % 7), 48000.0), "k.wav");
        if (blk % 30 == 7)
            p.loadWavetableFromMemory (encodeWav (saw, 48000.0), "saw.wav");
        p.processBlock (buf, m);
        for (int c = 0; c < 6; ++c)
            for (int i = 0; i < 256; ++i) finite = finite && std::isfinite (buf.getSample (c, i));
        if (blk % 3 == 0) p.collectGarbage();
    }
    p.processBlock (buf, m); p.processBlock (buf, m);
    p.collectGarbage();
    p.setPlayHead (nullptr);
    std::printf ("  400 Blöcke, 20 Sample- und 14 Wavetable-Wechsel: Ausgabe endlich = %s\n", finite ? "ja" : "nein");
    CHECK (finite, "NaN/Inf beim Austausch");
}

static void testWavetables()
{
    section ("Wavetables (bandbegrenzt, Morph, eigene Dateien)");
    const auto& b = builtinWavetables();
    CHECK (b.size() == (size_t) kNumBuiltinTables, "falsche Anzahl eingebauter Tabellen");

    // Bandbegrenzung: keine Energie oberhalb der erlaubten Obertöne je Mip-Stufe
    double worst = 0.0;
    for (const auto& t : b)
        for (int L = 0; L < WavetableData::kNumLevels; ++L)
        {
            const int n = t->size[(size_t) L];
            std::vector<std::complex<double>> a ((size_t) n);
            const float* d = t->level (t->numFrames / 2, L);
            for (int i = 0; i < n; ++i) a[(size_t) i] = d[i];
            fft (a, false);
            double inBand = 0, outBand = 0;
            for (int h = 1; h < n / 2; ++h)
                (h <= WavetableData::kMaxHarm[(size_t) L] ? inBand : outBand) += std::norm (a[(size_t) h]);
            worst = std::max (worst, outBand / std::max (1.0e-20, inBand));
        }
    std::printf ("  6 Tabellen x 9 Mip-Stufen: Energie oberhalb der Grenze relativ %.2g (soll ~0)\n", worst);
    CHECK (worst < 1.0e-10, "Wavetable nicht bandbegrenzt");

    // höchste erlaubte Frequenz je Stufe bleibt unter 0,45 x Samplerate
    bool ok = true;
    for (double f : { 20.0, 46.0, 110.0, 440.0, 2000.0, 8000.0 })
        for (double sr : { 44100.0, 96000.0 })
        {
            const int L = WavetableData::levelFor (f, sr);
            if (L < WavetableData::kNumLevels - 1 && WavetableData::kMaxHarm[(size_t) L] * f > 0.45 * sr) ok = false;
        }
    CHECK (ok, "Mip-Stufenwahl erlaubt Aliasing");

    // Morph verändert den Klang
    double diff = 0;
    for (int i = 0; i < 100; ++i) diff += std::abs (b[1]->read (0.0, i / 100.0, 0) - b[1]->read (1.0, i / 100.0, 0));
    CHECK (diff > 1.0, "Morph ohne Wirkung");

    // eigene Dateien
    std::vector<float> frames (2048 * 4);
    for (size_t i = 0; i < frames.size(); ++i) frames[i] = (float) std::sin (2.0 * 3.14159265358979 * (double) (i % 2048) / 2048.0 * (1 + (int) (i / 2048)));
    std::string info;
    auto t1 = wavetableFromAudio (frames, 48000.0, "frames", info);
    std::printf ("  Datei 4 x 2048 Samples: %s\n", info.c_str());
    CHECK (t1 != nullptr && t1->numFrames == 4, "2048er-Frames nicht erkannt");
    std::vector<float> saw (48000);
    for (size_t i = 0; i < saw.size(); ++i) saw[i] = (float) (1.0 - 2.0 * std::fmod ((double) i * 110.0 / 48000.0, 1.0));
    auto t2 = wavetableFromAudio (saw, 48000.0, "saw", info);
    std::printf ("  Saw-Sample 110 Hz, 1 s: %s\n", info.c_str());
    CHECK (t2 != nullptr && t2->numFrames > 8, "tonhöhensynchrone Zerlegung fehlgeschlagen");

    // Bass mit Wavetable bei sehr hohen und tiefen Noten: endlich, Pegel vernünftig
    SamsiBasslineProcessor p;
    setParam (p, pid::seqOn, 0.0f);
    setParam (p, pid::wtLevel, 1.0f);
    setParam (p, pid::wtTable, 4.0f);
    setParam (p, pid::wtEnv, 1.0f);
    auto midi = [] (int64_t start, int, juce::MidiBuffer& m)
    {
        if (start == 0) { m.addEvent (juce::MidiMessage::noteOn (1, 100, (juce::uint8) 120), 0); m.addEvent (juce::MidiMessage::noteOn (1, 12, (juce::uint8) 120), 200); }
    };
    const auto o = run (p, 44100.0, 22050, fixedBlock (512), 145.0, midi, nullptr, false);
    std::printf ("  Wavetable-Bass Note 100 + 12: Peak %.2f dBFS, endlich = %s\n",
                 juce::Decibels::gainToDecibels ((float) peak (o.ch[4])), allFinite (o) ? "ja" : "nein");
    CHECK (allFinite (o) && peak (o.ch[4]) < 2.0, "Wavetable-Bass instabil");
}

static void testPickDamper()
{
    section ("Pick und Dämpfer");
    auto render = [] (float pick, float damper)
    {
        SamsiBasslineProcessor p;
        setParam (p, pid::seqOn, 0.0f);
        setParam (p, pid::bSaw, 1.0f); setParam (p, pid::bSub, 0.0f);
        setParam (p, pid::bCutoff, 16000.0f); setParam (p, pid::bEnvAmt, 0.0f);
        setParam (p, pid::bSus, 1.0f); setParam (p, pid::bDec, 2000.0f);
        setParam (p, pid::pickAmt, pick); setParam (p, pid::pickPos, 0.25f); setParam (p, pid::damper, damper);
        auto midi = [] (int64_t start, int, juce::MidiBuffer& m) { if (start == 0) m.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 120), 0); };
        return run (p, 48000.0, 24000, fixedBlock (512), 145.0, midi, nullptr, false).ch[4];
    };
    const auto plain = render (0.0f, 0.0f), picked = render (1.0f, 0.0f), damped = render (0.0f, 1.0f);
    const double hfPlain = energyAbove (plain, 9600, 14400, 48000.0, 2000.0);
    const double hfDamp  = energyAbove (damped, 9600, 14400, 48000.0, 2000.0);
    double d = 0; for (size_t i = 0; i < plain.size(); ++i) d = std::max (d, (double) std::abs (plain[i] - picked[i]));
    std::printf ("  Pick verändert den Klang (max. Differenz %.3f); Höhen 200-300 ms mit/ohne Dämpfer: %.1f dB\n",
                 d, 10.0 * std::log10 (hfDamp / std::max (1.0e-20, hfPlain)));
    CHECK (d > 0.01, "Pick ohne Wirkung");
    CHECK (hfDamp < hfPlain * 0.5, "Dämpfer reduziert die Höhen nicht");
}

static void testKeyAnalysis()
{
    section ("Tonart-/Frequenz-Abgleich");
    Settings s;   // F# Phrygisch, Kick 46,25 Hz (F#0), Bass F#0
    auto r = analyzeKey (s, PatternData::makeDefault(), 0.0);
    std::printf ("  Standard: Kick %s (%+.1f ct), Bass %s, Gesamt %s\n", r.kick.name.c_str(), r.kick.cents, r.bassRoot.name.c_str(),
                 r.overall == Status::Ok ? "OK" : r.overall == Status::Warn ? "Warnung" : "ROT");
    CHECK (r.overall == Status::Ok, "Standard sollte passen");

    s.kick.endHz = 50.0f;   // zwischen G0 und G#0 -> verstimmt
    r = analyzeKey (s, PatternData::makeDefault(), 0.0);
    std::printf ("  Kick 50 Hz: %s\n", r.checks[0].detail.c_str());
    CHECK (r.checks[0].status == Status::Bad && r.overall == Status::Bad, "verstimmte Kick nicht rot");

    s.kick.endHz = 46.25f;
    auto pat = PatternData::makeDefault();
    Step st = pat.get (5); st.note = 2; pat.set (5, st);   // G# ist nicht in F# Phrygisch
    r = analyzeKey (s, pat, 0.0);
    std::printf ("  Pattern mit G#: %s\n", r.checks[2].detail.c_str());
    CHECK (r.stepOutOfKey[5] && r.checks[2].status == Status::Bad, "Ton außerhalb der Skala nicht erkannt");
    const auto fixed = snapPatternToScale (pat, s.global.rootNote, s.global.keyRoot, s.global.keyScale);
    CHECK (! analyzeKey (s, fixed, 0.0).stepOutOfKey[5], "Pattern-Korrektur ohne Wirkung");

    s.global.rootNote = 31;   // Bass G gegen Kick F# -> kleine Sekunde
    r = analyzeKey (s, PatternData::makeDefault(), 0.0);
    std::printf ("  Bass G0 gegen Kick F#0: %s\n", r.checks[3].title.c_str());
    CHECK (r.checks[3].status == Status::Bad, "Kick/Bass-Reibung nicht rot");

    const double k = kickHzForKey (5, 50.0);
    std::printf ("  Korrektur Kick -> F: %.2f Hz (erwartet 43,65); Bass -> F: MIDI %d (erwartet 29); Sample 57 Hz -> Tune %+.2f st\n",
                 k, bassRootForKey (5, 31), sampleTuneForKick (57.0, 46.25, 0.0));
    CHECK (std::abs (k - 43.65) < 0.05 && bassRootForKey (5, 31) == 29, "Korrekturwerte falsch");
    CHECK (std::abs (sampleTuneForKick (57.0, 46.25, 0.0) - 12.0 * std::log2 (46.25 / 57.0)) < 0.01, "Sample-Stimmung falsch");

    // Snap-Funktionen im Plugin
    SamsiBasslineProcessor p;
    setParam (p, pid::kEnd, 52.0f);
    setParam (p, pid::keyRoot, 9.0f);   // A
    p.snapKickToKey(); p.snapBassToKey();
    const auto s2 = p.readSettings();
    std::printf ("  Plugin: Kick -> %.2f Hz, Bass -> MIDI %d (Tonart A)\n", s2.kick.endHz, s2.global.rootNote);
    CHECK (std::abs (s2.kick.endHz - 55.0f) < 0.05f && s2.global.rootNote % 12 == 9, "Snap im Plugin fehlerhaft");
}

static void testPresets()
{
    section ("Werks-Presets");
    const auto& presets = factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        SamsiBasslineProcessor p;
        p.applyFactoryPreset (i);
        const auto rep = p.analyzeKey();
        const auto o = run (p, 48000.0, 96000, fixedBlock (512), 148.0);
        double pk = 0; for (auto& c : o.ch) pk = std::max (pk, peak (c));
        bool keyOk = true;
        for (size_t c = 0; c < 3 && c < rep.checks.size(); ++c) keyOk = keyOk && rep.checks[c].status != Status::Bad;
        std::printf ("  %-22s Peak %+5.1f dBFS, Kick %s %+.0f ct, Bass %s, Tonart-Prüfung %s\n", presets[(size_t) i].name.c_str(),
                     juce::Decibels::gainToDecibels ((float) pk), rep.kick.name.c_str(), rep.kick.cents, rep.bassRoot.name.c_str(),
                     keyOk ? "ok" : "ROT");
        CHECK (p.getCurrentPreset() == i, "Preset nicht übernommen");
        CHECK (allFinite (o) && pk < 1.0, "Preset %d: Pegel über 0 dBFS oder NaN", i);
        CHECK (keyOk, "Preset %d nicht in seiner Tonart", i);
    }
    // Host-Programmwechsel dürfen nichts überschreiben (Presets nur über die Oberfläche)
    SamsiBasslineProcessor p;
    p.applyFactoryPreset (1);
    setParam (p, pid::bCutoff, 3333.0f);
    p.setCurrentProgram (3);
    CHECK (std::abs (p.apvts.getRawParameterValue (pid::bCutoff)->load() - 3333.0f) < 1.0f, "Host-Programmwechsel hat Einstellungen überschrieben");

    // eigene Presetdatei
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samsi_test.samsipreset");
    setParam (p, pid::wtPos, 0.77f);
    CHECK (p.savePresetFile (f), "Preset nicht gespeichert");
    SamsiBasslineProcessor q;
    const auto err = q.loadPresetFile (f);
    std::printf ("  Eigenes Preset gespeichert/geladen: %s, Morph = %.2f\n", err.isEmpty() ? "ok" : err.toRawUTF8(),
                 q.apvts.getRawParameterValue (pid::wtPos)->load());
    CHECK (err.isEmpty() && std::abs (q.apvts.getRawParameterValue (pid::wtPos)->load() - 0.77f) < 0.01f, "Presetdatei fehlerhaft");
    f.deleteFile();
}

static void testGateModes()
{
    section ("MIDI-Gate (eine durchgehende Note) und Transponieren");
    auto runGate = [] (int mode, int heldNote, TriggerLog& log)
    {
        SamsiEngine e; e.prepare (48000.0, 512);
        e.setTriggerLog (&log);
        Settings s; s.global.seqMode = mode;
        const auto pat = PatternData::makeDefault();
        TransportInfo t; t.playing = false; t.bpm = 145.0;   // Host steht -> interne Uhr
        for (int pos = 0; pos < 72000; pos += 480)
        {
            MidiEvt m[2]; int nm = 0;
            if (pos <= 4800 && 4800 < pos + 480)   m[nm++] = { 4800 - pos, true, heldNote, 100 };
            if (pos <= 52800 && 52800 < pos + 480) m[nm++] = { 52800 - pos, false, heldNote, 0 };
            e.process (s, pat, t, m, nm, 480);
        }
    };
    TriggerLog a; a.kickOn.reserve (64); a.bassOn.reserve (64); a.bassOff.reserve (64); a.bassNotes.reserve (64);
    runGate (1, 40, a);
    std::printf ("  Gate, Note 1 s gehalten (145 BPM, Host gestoppt): %zu Kicks, %zu Bassnoten (erwartet 3 / 7), erster Kick bei Sample %lld (erwartet 4800)\n",
                 a.kickOn.size(), a.bassOn.size(), a.kickOn.empty() ? -1LL : (long long) a.kickOn[0]);
    CHECK (a.kickOn.size() == 3 && a.bassOn.size() == 7 && ! a.kickOn.empty() && a.kickOn[0] == 4800, "Gate-Modus zählt falsch");
    CHECK (a.bassOn.empty() || a.bassOn.back() < 52800, "Noten nach Gate-Ende");

    TriggerLog b; b.kickOn.reserve (64); b.bassOn.reserve (64); b.bassOff.reserve (64); b.bassNotes.reserve (64);
    runGate (2, 64, b);   // E (Tonhöhenklasse 4) -> Grundton E nahe F#0 = MIDI 28
    std::printf ("  Gate + Transpose mit Note E3 (64): Bass-Grundton MIDI %d (erwartet 28 = E0)\n", b.bassNotes.empty() ? -1 : b.bassNotes[0]);
    CHECK (! b.bassNotes.empty() && b.bassNotes[0] == 28, "Transponieren falsch");
}

static void testLookahead()
{
    section ("Lookahead für Live-MIDI (Latenz + negativer Bass-Versatz)");
    TriggerLog log; log.kickOn.reserve (16); log.bassOn.reserve (16); log.bassOff.reserve (16); log.bassNotes.reserve (16);
    SamsiEngine e; e.prepare (48000.0, 512);
    e.setTriggerLog (&log);
    Settings s; s.global.seqOn = false; s.global.lookaheadMs = 10; s.fit.offsetMs = -5.0f;
    TransportInfo t; t.playing = false;
    for (int pos = 0; pos < 4096; pos += 512)
    {
        MidiEvt m[2]; int nm = 0;
        if (pos == 0) { m[nm++] = { 100, true, 36, 110 }; m[nm++] = { 100, true, 40, 100 }; }
        e.process (s, PatternData::makeEmpty(), t, m, nm, 512);
    }
    std::printf ("  MIDI bei Sample 100, Lookahead 10 ms (480), Bass-Versatz -5 ms: Kick bei %lld (erwartet 580), Bass bei %lld (erwartet 340)\n",
                 log.kickOn.empty() ? -1LL : (long long) log.kickOn[0], log.bassOn.empty() ? -1LL : (long long) log.bassOn[0]);
    CHECK (! log.kickOn.empty() && log.kickOn[0] == 580 && ! log.bassOn.empty() && log.bassOn[0] == 340, "Lookahead-Zeitplanung falsch");

    SamsiBasslineProcessor p;
    setParam (p, pid::lookahead, 2.0f);   // 10 ms
    p.prepareToPlay (48000.0, 512);
    std::printf ("  Plugin meldet Latenz: %d Samples (erwartet 480)\n", p.getLatencySamples());
    CHECK (p.getLatencySamples() == 480, "Latenz nicht gemeldet");
    setParam (p, pid::lookahead, 0.0f);
    p.prepareToPlay (48000.0, 512);
    CHECK (p.getLatencySamples() == 0, "Latenz nicht zurückgesetzt");
}

static void testGrids()
{
    section ("Sequencer-Raster 1/16-Triole und 1/32");
    struct Case { int grid; int bassPerBeat; const char* name; };
    for (const Case c : { Case { 1, 4, "1/16-Triole (K-.-B-B-B-B)" }, Case { 2, 5, "1/32 (K-.-.-B-B-B-B-B)" } })
    {
        const double sr = 48000.0, bpm = 185.0, spq = 60.0 / bpm * sr;
        const int total = (int) std::lround (spq * 16.0);   // 4 Takte
        auto render = [&] (std::function<int (int)> blk)
        {
            SamsiEngine e; e.prepare (sr, 512);
            TriggerLog log; log.kickOn.reserve (256); log.bassOn.reserve (1024); log.bassOff.reserve (1024); log.bassNotes.reserve (1024);
            e.setTriggerLog (&log);
            Settings s; s.global.seqGrid = c.grid;
            const auto pat = PatternData::makeRolling (c.grid, false);
            int64_t pos = 0; int bi = 0;
            while (pos < total)
            {
                const int n = std::min ({ blk (bi++), (int) (total - pos), 512 });
                TransportInfo t; t.playing = true; t.hasPpq = true; t.bpm = bpm; t.ppq = (double) pos / spq;
                e.process (s, pat, t, nullptr, 0, n);
                pos += n;
            }
            return log;
        };
        std::mt19937 rng (11);
        std::uniform_int_distribution<int> dist (1, 600);
        const auto a = render ([] (int) { return 512; });
        const auto b = render ([&] (int) { return dist (rng); });
        const double stepQn = stepQnForGrid (c.grid);
        const int firstBassSlot = c.grid == 1 ? 2 : 3;
        const double expectFirst = firstBassSlot * stepQn * spq;
        std::printf ("  %-28s %zu Kicks, %zu Bassnoten (erwartet 16 / %d), erste Bassnote bei %lld (erwartet %.1f), Blockgrößen-unabhängig: %s\n",
                     c.name, a.kickOn.size(), a.bassOn.size(), 16 * c.bassPerBeat,
                     a.bassOn.empty() ? -1LL : (long long) a.bassOn[0], expectFirst,
                     (a.kickOn == b.kickOn && a.bassOn == b.bassOn) ? "ja" : "NEIN");
        CHECK (a.kickOn.size() == 16 && (int) a.bassOn.size() == 16 * c.bassPerBeat, "Raster %d: Triggeranzahl falsch", c.grid);
        CHECK (! a.bassOn.empty() && std::abs ((double) a.bassOn[0] - expectFirst) <= 1.0, "Raster %d: Position ungenau", c.grid);
        CHECK (a.kickOn == b.kickOn && a.bassOn == b.bassOn, "Raster %d: abhängig von der Blockgröße", c.grid);
    }
}

static std::vector<float> makeTestBassNote (double rate, double hz, double seconds = 0.6)
{
    // tonale Bassnote: bandbegrenzter Sägezahn (8 Obertöne) mit Abklingen
    std::vector<float> x ((size_t) (rate * seconds));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / rate;
        double v = 0.0;
        for (int h = 1; h <= 8; ++h) v += std::sin (2.0 * juce::MathConstants<double>::pi * hz * h * t) / h;
        x[i] = (float) (0.5 * v * std::exp (-t * 3.0) * std::min (1.0, t / 0.002));
    }
    return x;
}

static void testBassSample()
{
    section ("Bass-Sample: Grundton, Stimmung pro Note, reproduzierbare Startphase");
    SamsiBasslineProcessor p;
    const auto err = p.loadBassSampleFromMemory (encodeWav (makeTestBassNote (44100.0, 55.0), 44100.0), "bassnote.wav");
    auto bs = p.getBassSample();
    std::printf ("  geladen: %s, erkannter Grundton %.2f Hz (erwartet 55), Konfidenz %.2f\n",
                 err.isEmpty() ? "ja" : err.toRawUTF8(), bs ? bs->pitchHz : 0.0f, bs ? bs->pitchConfidence : 0.0f);
    CHECK (err.isEmpty() && bs != nullptr && std::abs (bs->pitchHz - 55.0f) < 1.5f, "Bass-Sample-Grundton falsch");
    if (bs == nullptr) return;

    // nur das Sample hörbar: Oszillatoren aus, Filter offen
    setParam (p, pid::bSaw, 0.0f); setParam (p, pid::bSub, 0.0f); setParam (p, pid::bsLevel, 1.0f);
    setParam (p, pid::bCutoff, 16000.0f); setParam (p, pid::bEnvAmt, 0.0f); setParam (p, pid::bDrive, 0.0f);
    setParam (p, pid::bSus, 1.0f); setParam (p, pid::bDec, 2000.0f);
    // eine lange Bassnote auf dem Grundton (F#0 = 46,25 Hz)
    PatternData pat = PatternData::makeEmpty();
    { Step s; s.bass = true; s.lenPct = 300; pat.set (1, s); }
    p.setPattern (pat);
    const auto o1 = run (p, 48000.0, 48000, fixedBlock (512), 120.0);
    const auto o2 = run (p, 48000.0, 48000, fixedBlock (333), 120.0);
    const int noteStart = (int) std::lround (0.25 * 60.0 / 120.0 * 48000.0);
    const auto pr = detectPitch (o1.ch[4], 48000.0, noteStart + 2400, 9600, 25.0, 200.0);
    std::printf ("  gespielte Tonhöhe auf F#0: %.2f Hz (erwartet 46.25), Wiederholung mit anderer Blockgröße: max. Abweichung %.3g\n",
                 pr.hz, maxAbsDiff (o1.ch[4], o2.ch[4]));
    CHECK (std::abs (pr.hz - 46.25) < 1.0, "Bass-Sample nicht auf die Note gestimmt");
    CHECK (maxAbsDiff (o1.ch[4], o2.ch[4]) < 1.0e-5, "Bass-Sample nicht reproduzierbar");

    // Startphase verschiebt den Sample-Startpunkt (wie beim Oszillator) -> Auto Fit kann ihn nutzen
    setParam (p, pid::bPhase, 180.0f);
    const auto o3 = run (p, 48000.0, 48000, fixedBlock (512), 120.0);
    const double d = maxAbsDiff (o1.ch[4], o3.ch[4]);
    std::printf ("  Startphase 0 -> 180 Grad ändert das Bass-Signal: max. Differenz %.3f\n", d);
    CHECK (d > 0.05, "Startphase wirkt nicht auf das Bass-Sample");
}

static void testKickAlign()
{
    section ("Kick-Sample: automatische Phasenausrichtung (Laden, Knopf, Auto Fit, Undo)");
    // a) Sample = gerenderte Synth-Kick (44,1 kHz): es gibt eine exakte Lösung, die gefunden werden muss
    std::vector<float> synthKick;
    {
        KickSettings k; k.endHz = 46.25f;
        KickEngine e; e.prepare (44100.0);
        e.trigger (k, 1.0f, 1.0f, nullptr);
        synthKick.resize (22050);
        e.render (synthKick.data(), (int) synthKick.size(), k);
    }
    SamsiBasslineProcessor p;
    p.prepareToPlay (48000.0, 512);
    setParam (p, pid::kEnd, 46.25f);
    p.loadKickSampleFromMemory (encodeWav (synthKick, 44100.0), "synthkick.wav");
    setParam (p, pid::ksOn, 1.0f);
    std::printf ("  nach dem Laden automatisch ausgerichtet: Verzögerung %.2f ms, Start %.2f ms\n",
                 p.apvts.getRawParameterValue (pid::ksOffset)->load(), p.apvts.getRawParameterValue (pid::ksStart)->load());
    setParam (p, pid::ksOffset, 7.0f);                 // bewusst verschieben ...
    setParam (p, pid::ksInvert, 1.0f);                 // ... und invertieren
    const auto r = p.alignKickSampleNow();
    std::printf ("  AUSRICHTEN (identische Kick, 7 ms + invertiert): Korrelation %.2f -> %.2f, Verzögerung %.2f ms, invertiert %s\n",
                 r.coherenceBefore, r.coherenceAfter, r.delayMs, r.invert ? "ja" : "nein");
    CHECK (r.valid && r.coherenceAfter > 0.97 && r.delayMs < 0.15f && ! r.invert, "Ausrichtung findet die exakte Lage nicht");

    // b) fremde Kick (anderer Pitch-Verlauf): keine exakte Lösung, aber Verbesserung
    {
        SamsiBasslineProcessor q;
        q.prepareToPlay (48000.0, 512);
        q.loadKickSampleFromMemory (encodeWav (makeTestKick (44100.0, 46.25), 44100.0), "fremd.wav");
        setParam (q, pid::ksOffset, 7.0f);
        setParam (q, pid::ksInvert, 1.0f);
        const auto r2 = q.alignKickSampleNow();
        std::printf ("  AUSRICHTEN (fremde Kick): Korrelation %.2f -> %.2f (Pitch-Verläufe verschieden -> keine perfekte Deckung möglich)\n",
                     r2.coherenceBefore, r2.coherenceAfter);
        CHECK (r2.valid && r2.coherenceAfter > r2.coherenceBefore + 0.2, "Ausrichtung verbessert die Korrelation nicht");
    }

    // c) Auto Fit richtet das Sample ebenfalls aus; Undo stellt die vorherige Lage wieder her
    setParam (p, pid::ksOffset, 7.0f);
    p.startAutoFit();
    const auto t0 = juce::Time::getMillisecondCounter();
    bool got = false;
    while (juce::Time::getMillisecondCounter() - t0 < 120000)
    {
        juce::Thread::sleep (50);
        if (p.pollFitResult()) { got = true; break; }
    }
    const float afterFit = p.apvts.getRawParameterValue (pid::ksOffset)->load();
    const auto fit = p.getLastFit();
    std::printf ("  Auto Fit: Sample-Verzögerung 7.00 -> %.2f ms (Korrelation %.2f -> %.2f)\n", afterFit,
                 fit ? fit->kickAlign.coherenceBefore : 0.0, fit ? fit->kickAlign.coherenceAfter : 0.0);
    CHECK (got && fit != nullptr && fit->kickAlign.valid && afterFit < 0.15f, "Auto Fit richtet das Kick-Sample nicht aus");
    p.undoFit();
    const float undone = p.apvts.getRawParameterValue (pid::ksOffset)->load();
    std::printf ("  Undo: Verzögerung wieder %.2f ms\n", undone);
    CHECK (std::abs (undone - 7.0f) < 0.02f, "Undo stellt die Sample-Lage nicht wieder her");
}

static void testFactoryPresetSet()
{
    section ("Werks-Presets: 25 aus Referenzen + Init + Werkstatt");
    const auto& presets = factoryPresets();
    std::map<std::string, int> themes;
    for (auto& pr : presets) themes[pr.theme]++;
    int generated = 0;
    for (auto& t : themes) if (t.first != "Init" && t.first != "Werkstatt") generated += t.second;
    std::printf ("  %zu Presets gesamt, %d aus den Referenzmessungen in %zu Themen\n", presets.size(), generated, themes.size() - 2);
    CHECK (generated == 25, "Es müssen 25 gemessene Presets sein");
    // jedes erzeugte Preset: Kick-Grundton und Bass-Grundton gleiche Tonhöhenklasse
    bool allMatch = true;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].theme == "Init" || presets[(size_t) i].theme == "Werkstatt") continue;
        SamsiBasslineProcessor p;
        p.applyFactoryPreset (i);
        const auto s = p.readSettings();
        const double kickMidi = 69.0 + 12.0 * std::log2 (s.kick.endHz / 440.0);
        const int pcK = ((int) std::lround (kickMidi) % 12 + 12) % 12, pcB = (s.global.rootNote % 12 + 12) % 12;
        if (pcK != pcB || std::abs (kickMidi - std::round (kickMidi)) > 0.05) { allMatch = false; std::printf ("  %s: Kick %d / Bass %d\n", presets[(size_t) i].name.c_str(), pcK, pcB); }
    }
    CHECK (allMatch, "Kick- und Bass-Grundton passen nicht zusammen");
}

//==============================================================================
// Ausgangs-Utility, Limiter, Live-Analyse
//==============================================================================
static double phaseOf (const std::vector<float>& x, size_t from, size_t to, double f, double sr, double& amp)
{
    double c = 0, s = 0;
    for (size_t i = from; i < to; ++i) { const double w = 2.0 * juce::MathConstants<double>::pi * f * (double) i / sr; c += x[i] * std::cos (w); s += x[i] * std::sin (w); }
    const double n = (double) (to - from);
    amp = 2.0 * std::sqrt (c * c + s * s) / n;
    return std::atan2 (-s, c);
}

static void testUtilityChain()
{
    section ("Ausgangs-Utility: Nulltest mit allen linearen Stufen, Latenz");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        SamsiBasslineProcessor p;
        setParam (p, pid::bWidth, 0.7f);
        setParam (p, pid::uImager, 1.7f); setParam (p, pid::uTilt, 3.0f);
        setParam (p, pid::uPhaseOn, 1.0f); setParam (p, pid::uPhase, 37.0f);
        setParam (p, pid::uDc, 1.0f); setParam (p, pid::uMonoOn, 1.0f); setParam (p, pid::uMonoHz, 150.0f);
        setParam (p, pid::uSwap, 1.0f); setParam (p, pid::uInvL, 1.0f); setParam (p, pid::uMix, 0.7f);
        setParam (p, pid::uOutL, -3.0f); setParam (p, pid::uTsOn, 1.0f); setParam (p, pid::uTs, -12.0f);
        const auto o = run (p, sr, (int) (sr * 2.0), [] (int b) { return 64 + (b * 97) % 700; });
        double d = 0;
        for (size_t i = 0; i < o.size(); ++i)
            for (int c = 0; c < 2; ++c) d = std::max (d, (double) std::abs (o.ch[c][i] - (o.ch[2 + c][i] + o.ch[4 + c][i])));
        p.prepareToPlay (sr, 512);
        const int lat = p.getLatencySamples();
        std::printf ("  %6.0f Hz: max |MIX-(KICK+BASS)| = %.2g (alle linearen Stufen an), gemeldete Latenz %d Samples (erwartet %d = 50 ms), Peak %.1f dBFS, endlich %s\n",
                     sr, d, lat, (int) std::lround (0.05 * sr), juce::Decibels::gainToDecibels ((float) peak (o.ch[0])), allFinite (o) ? "ja" : "NEIN");
        CHECK (d < 2.0e-5 && allFinite (o), "Utility verletzt MIX = KICK + BASS");
        CHECK (lat == (int) std::lround (0.05 * sr), "Time-Shift-Latenz falsch");
    }
    // Utility neutral -> bitgleich zur alten Ausgabe
    {
        SamsiBasslineProcessor a, b;
        setParam (b, pid::uOn, 0.0f);
        const auto oa = run (a, 48000.0, 48000, fixedBlock (512));
        const auto ob = run (b, 48000.0, 48000, fixedBlock (512));
        double d = 0; for (int c = 0; c < 6; ++c) d = std::max (d, maxAbsDiff (oa.ch[c], ob.ch[c]));
        std::printf ("  Utility neutral vs. aus: Abweichung %.3g (erwartet 0)\n", d);
        CHECK (d == 0.0, "neutrale Utility verändert das Signal");
    }
}

static void testUtilityStages()
{
    section ("Utility-Stufen einzeln: Phasendrehung, Time Shift, Tilt, Imager, Mono-Filter, DC, Listen");
    const double sr = 48000.0;
    auto runUtil = [sr] (const UtilitySettings& u, std::vector<float> l, std::vector<float> r, int base = 0)
    {
        BusUtility bu; bu.prepare (sr, 512); bu.reset (u);
        for (size_t off = 0; off < l.size(); off += 256)
        {
            const int n = (int) std::min<size_t> (256, l.size() - off);
            bu.process (l.data() + off, r.data() + off, n, u, base);
        }
        return std::make_pair (l, r);
    };
    auto sine = [sr] (double f, int n, double a = 0.5) { std::vector<float> x ((size_t) n); for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (a * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr)); return x; };

    // Phasendrehung 90° gegenüber 0° (beide eingeschaltet), 30 Hz .. 10 kHz
    double worstDeg = 0, worstDb = 0;
    for (double f : { 30.0, 60.0, 120.0, 1000.0, 10000.0 })
    {
        const int n = (int) sr * 2;
        UtilitySettings u0; u0.phaseOn = true; u0.phaseDeg = 0.0f;
        UtilitySettings u9 = u0; u9.phaseDeg = 90.0f;
        const auto a = runUtil (u0, sine (f, n), sine (f, n)).first;
        const auto b = runUtil (u9, sine (f, n), sine (f, n)).first;
        double ampA, ampB;
        const double pa = phaseOf (a, (size_t) sr, (size_t) n, f, sr, ampA), pb = phaseOf (b, (size_t) sr, (size_t) n, f, sr, ampB);
        double dDeg = (pb - pa) * 180.0 / juce::MathConstants<double>::pi;
        while (dDeg < -180) dDeg += 360; while (dDeg > 180) dDeg -= 360;
        worstDeg = std::max (worstDeg, std::abs (dDeg - 90.0));
        worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (ampB / ampA)));
        worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (ampA / 0.5)));
    }
    std::printf ("  Phasendrehung 90°: größte Abweichung %.3f° und %.4f dB (30 Hz..10 kHz, 48 kHz)\n", worstDeg, worstDb);
    CHECK (worstDeg < 0.5 && worstDb < 0.05, "Phasendrehung ungenau");

    // Time Shift: Impuls, Grundlatenz 50 ms, Verschiebung -20 ms -> Ausgang nach 30 ms
    {
        const int base = (int) std::lround (0.05 * sr);
        std::vector<float> l ((size_t) sr / 4, 0.0f), r = l;
        l[100] = r[100] = 1.0f;
        UtilitySettings u; u.timeShiftOn = true; u.timeShiftMs = -20.0f; u.tiltMs = 2.0f;
        BusUtility bu; bu.prepare (sr, 512); bu.reset (u);
        // Glätter auf Ziel: einmal Stille durchlaufen lassen
        std::vector<float> zl ((size_t) sr / 2, 0.0f), zr = zl;
        bu.process (zl.data(), zr.data(), (int) zl.size(), u, base);
        bu.process (l.data(), r.data(), (int) l.size(), u, base);
        auto argmax = [] (const std::vector<float>& x) { size_t m = 0; for (size_t i = 0; i < x.size(); ++i) if (std::abs (x[i]) > std::abs (x[m])) m = i; return (int) m; };
        const int pl = argmax (l), pr = argmax (r);
        std::printf ("  Time Shift -20 ms bei 50 ms Grundlatenz: Impuls L bei %d (erwartet %d), Tilt +2 ms: R bei %d (erwartet %d)\n",
                     pl, 100 + base - 960, pr, 100 + base - 960 + 96);
        CHECK (pl == 100 + base - 960 && pr == pl + 96, "Time Shift / Tilt ungenau");
    }

    // Imager 0 -> mono; Mono-Filter: Seitensignal bei 30 Hz weg, bei 1 kHz erhalten
    {
        const int n = (int) sr;
        auto l = sine (1000.0, n), r = sine (300.0, n);
        UtilitySettings u; u.imager = 0.0f;
        const auto o = runUtil (u, l, r);
        CHECK (maxAbsDiff (o.first, o.second) < 1.0e-6, "Imager 0 % ist nicht mono");
        UtilitySettings m; m.monoOn = true; m.monoHz = 150.0f;
        auto sl = sine (30.0, n), sr2 = sl; for (auto& v : sr2) v = -v;             // reines Seitensignal 30 Hz
        auto hl = sine (1000.0, n), hr = hl; for (auto& v : hr) v = -v;             // reines Seitensignal 1 kHz
        const auto lo = runUtil (m, sl, sr2), hi = runUtil (m, hl, hr);
        const double aLo = 20.0 * std::log10 (rms (lo.first, (size_t) sr / 2, (size_t) n) / rms (sl, (size_t) sr / 2, (size_t) n));
        const double aHi = 20.0 * std::log10 (rms (hi.first, (size_t) sr / 2, (size_t) n) / rms (hl, (size_t) sr / 2, (size_t) n));
        std::printf ("  Imager 0 %%: L = R; Mono-Filter 150 Hz: Seitensignal 30 Hz %.1f dB, 1 kHz %.2f dB\n", aLo, aHi);
        CHECK (aLo < -40.0 && std::abs (aHi) < 0.1, "Mono-Filter ungenau");
    }
    // DC-Sperre und Listen M
    {
        const int n = (int) sr * 2;
        std::vector<float> l ((size_t) n, 0.3f), r ((size_t) n, 0.3f);
        UtilitySettings u; u.dcBlock = true;
        const auto o = runUtil (u, l, r);
        UtilitySettings lm; lm.listen = 3;
        const auto m = runUtil (lm, sine (100.0, 4800), sine (250.0, 4800));
        const auto s100 = sine (100.0, 4800), s250 = sine (250.0, 4800);
        double dm = 0; for (size_t i = 0; i < 4800; ++i) dm = std::max (dm, (double) std::abs (m.first[i] - 0.5f * (s100[i] + s250[i])));
        std::printf ("  DC 0,3 nach 1 s: %.5f; Listen M: Abweichung %.2g\n", o.first[(size_t) sr], dm);
        CHECK (std::abs (o.first[(size_t) sr]) < 1.0e-3 && dm < 1.0e-6 && maxAbsDiff (m.first, m.second) == 0.0, "DC-Sperre oder Listen M falsch");
    }
}

static void testLimiter()
{
    section ("Limiter (nur MIX, nichtlinear): Ceiling, Latenz, Ausrichtung von KICK/BASS");
    const double sr = 48000.0;
    Limiter lim; lim.prepare (sr);
    std::mt19937 rng (3);
    std::normal_distribution<float> nd (0.0f, 0.6f);
    std::vector<float> l (96000), r (96000);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = nd (rng) * (i % 9000 < 300 ? 4.0f : 1.0f); r[i] = nd (rng); }
    auto ol = l, orr = r;
    for (size_t off = 0; off < l.size(); off += 333) lim.process (ol.data() + off, orr.data() + off, (int) std::min<size_t> (333, l.size() - off), -1.0f);
    const double pk = std::max (peak (ol), peak (orr));
    std::printf ("  Rauschen mit Spitzen bis %.1f dBFS -> Ausgang %.3f dBFS (Ceiling -1,0), Latenz %d Samples\n",
                 juce::Decibels::gainToDecibels ((float) peak (l)), juce::Decibels::gainToDecibels ((float) pk), Limiter::latencySamples (sr));
    CHECK (pk <= std::pow (10.0, -1.0 / 20.0) + 1.0e-6, "Limiter überschreitet Ceiling");

    SamsiBasslineProcessor p;
    setParam (p, pid::uLimOn, 1.0f); setParam (p, pid::uLimCeil, -6.0f); setParam (p, pid::master, 6.0f);
    const auto o = run (p, sr, 48000, fixedBlock (512));
    p.prepareToPlay (sr, 512);
    const int lat = p.getLatencySamples();
    SamsiBasslineProcessor q;
    setParam (q, pid::master, 6.0f);
    const auto oq = run (q, sr, 48000, fixedBlock (512));
    // KICK/BASS des Limiter-Laufs = um die Latenz verzögerte Ausgänge ohne Limiter
    double d = 0;
    for (size_t i = (size_t) lat; i < o.size(); ++i) d = std::max (d, (double) std::abs (o.ch[2][i] - oq.ch[2][i - (size_t) lat]));
    std::printf ("  im Plugin: MIX-Peak %.2f dBFS (Ceiling -6), Latenz %d, KICK zeitgleich verzögert: Abweichung %.2g\n",
                 juce::Decibels::gainToDecibels ((float) std::max (peak (o.ch[0]), peak (o.ch[1]))), lat, d);
    CHECK (std::max (peak (o.ch[0]), peak (o.ch[1])) <= std::pow (10.0, -6.0 / 20.0) + 1.0e-6 && lat == Limiter::latencySamples (sr) && d < 1.0e-7,
           "Limiter im Plugin fehlerhaft");
}

static void testLoudnessAndMeters()
{
    section ("Messung: LUFS (EBU R128), True Peak, RMS, Korrelation, Spektrum-Kalibrierung");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        LiveAnalyzer an; an.prepare (sr);
        const int n = (int) (sr * 5.0);
        std::vector<TapFrame> fr ((size_t) n);
        const double a = std::pow (10.0, -23.0 / 20.0);
        for (int i = 0; i < n; ++i) { const float v = (float) (a * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr)); fr[(size_t) i].mixL = fr[(size_t) i].mixR = v; }
        LiveTap tap (1 << 20); tap.setEnabled (true);
        for (int off = 0; off < n; off += 4096) tap.push (fr.data() + off, std::min (4096, n - off));
        an.drain (tap);
        const auto& lv = an.levels();
        std::printf ("  %6.0f Hz, Stereo-Sinus 1 kHz -23 dBFS: M %.2f, S %.2f, I %.2f LUFS (erwartet -23,0); Korrelation %+.3f\n",
                     sr, lv.lufsM, lv.lufsS, lv.lufsI, lv.correlation);
        CHECK (std::abs (lv.lufsM + 23.0) < 0.1 && std::abs (lv.lufsS + 23.0) < 0.1 && std::abs (lv.lufsI + 23.0) < 0.1, "LUFS ungenau");
        CHECK (lv.correlation > 0.999f && lv.widthPct < 0.1f, "Korrelation bei Mono falsch");
    }
    {
        // True Peak: Sinus bei fs/4 mit 45° -> Samples bei ±0,707, echte Spitze 1,0
        LiveAnalyzer an; an.prepare (48000.0);
        std::vector<TapFrame> fr (48000);
        for (int i = 0; i < 48000; ++i) { const float v = (float) std::sin (juce::MathConstants<double>::pi * 0.5 * i + juce::MathConstants<double>::pi * 0.25); fr[(size_t) i].mixL = v; fr[(size_t) i].mixR = -v; }
        an.feed (fr.data(), (int) fr.size());
        std::printf ("  Sinus fs/4 (Inter-Sample-Spitze): Sample-Peak %.2f dBFS, True Peak %.2f dBTP (erwartet ≈ 0); Korrelation bei L = -R: %+.3f\n",
                     an.levels().samplePeakDb, an.levels().truePeakDb, an.levels().correlation);
        CHECK (std::abs (an.levels().samplePeakDb + 3.01f) < 0.05f && std::abs (an.levels().truePeakDb) < 0.3f, "True Peak ungenau");
        CHECK (an.levels().correlation < -0.999f, "Korrelation bei Gegenphase falsch");
    }
    {
        // Spektrum: Sinus -6 dBFS auf einem Bin -> -6 dB für alle Fenster; Flattop auch zwischen den Bins
        LiveAnalyzer an; an.prepare (48000.0);
        const int N = 8192;
        auto feedSine = [&] (double f)
        {
            std::vector<TapFrame> fr (20000);
            for (int i = 0; i < 20000; ++i) { const float v = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / 48000.0)); fr[(size_t) i].mixL = fr[(size_t) i].mixR = v; }
            an.feed (fr.data(), (int) fr.size());
        };
        double worst = 0;
        const double fBin = 171.0 * 48000.0 / N;
        feedSine (fBin);
        std::vector<float> db;
        for (auto w : { LiveAnalyzer::Window::Hann, LiveAnalyzer::Window::Hamming, LiveAnalyzer::Window::BlackmanHarris,
                        LiveAnalyzer::Window::Flattop, LiveAnalyzer::Window::Rectangular })
        {
            an.computeSpectrum (-1, 13, w, db);
            worst = std::max (worst, std::abs (*std::max_element (db.begin(), db.end()) + 6.02));
        }
        feedSine (fBin + 0.5 * 48000.0 / N);
        an.computeSpectrum (-1, 13, LiveAnalyzer::Window::Flattop, db);
        const double flat = std::abs (*std::max_element (db.begin(), db.end()) + 6.02);
        std::printf ("  Spektrum-Kalibrierung (Sinus -6,02 dBFS): größte Abweichung aller 5 Fenster %.3f dB; Flattop zwischen zwei Bins %.3f dB\n", worst, flat);
        CHECK (worst < 0.05 && flat < 0.1, "Spektrum falsch kalibriert");
    }
}

static void testLiveTapAndCaptures()
{
    section ("Live-Anzeigen: Beat-Modus, Kick-/Bass-Mitschnitte, Tap im Audio-Thread");
    SamsiBasslineProcessor p;
    p.liveTap.setEnabled (true);
    LiveAnalyzer an; an.prepare (48000.0);
    TestPlayHead ph; ph.bpm = 145.0;
    p.setPlayHead (&ph);
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (6, 512);
    juce::MidiBuffer midi;
    const double spq = 60.0 / 145.0 * 48000.0;
    for (int64_t pos = 0; pos < 48000 * 3; pos += 512)
    {
        ph.ppq = (double) pos / spq;
        p.processBlock (buf, midi);
        an.drain (p.liveTap);
    }
    p.setPlayHead (nullptr);
    const auto& bk = an.beatTrace (LiveAnalyzer::Kick);
    double eStart = 0, eEnd = 0;
    for (int i = 0; i < 160; ++i) eStart += bk[(size_t) i] * bk[(size_t) i];
    for (int i = 1600; i < 2048; ++i) eEnd += bk[(size_t) i] * bk[(size_t) i];
    const auto* kc = an.kickCapture (0);
    const auto* bc = an.bassCapture (0);
    std::printf ("  Beat-Modus: Kick-Energie im ersten 8tel des Viertels %.3g vs. letztes Fünftel %.3g; Beat-Daten gültig: %s\n",
                 eStart, eEnd, an.beatValid() ? "ja" : "nein");
    std::printf ("  Kick-Mitschnitt: Peak %.1f dBFS, Länge %.0f ms, Grundton %.2f Hz (eingestellt 46,25); Bass-Mitschnitt: Länge %.0f ms, Grundton %.2f Hz (F#0 = 46,25)\n",
                 kc ? kc->peakDb : -999.f, kc ? kc->lengthMs : 0.f, kc ? kc->pitchHz : 0.f, bc ? bc->lengthMs : 0.f, bc ? bc->pitchHz : 0.f);
    std::printf ("  verworfene Tap-Werte: %lld\n", (long long) p.liveTap.droppedFrames());
    CHECK (an.beatValid() && eStart > 20.0 * eEnd, "Beat-Modus: Kick nicht am Viertelanfang");
    CHECK (kc != nullptr && std::abs (kc->pitchHz - 46.25f) < 1.5f && kc->lengthMs > 50.0f, "Kick-Mitschnitt fehlerhaft");
    CHECK (bc != nullptr && std::abs (bc->pitchHz - 46.25f) < 1.5f, "Bass-Mitschnitt fehlerhaft");
    CHECK (p.liveTap.droppedFrames() == 0, "Tap hat Werte verworfen");
}

static void testSidechainAndTempo()
{
    section ("Sidechain (nur Anzeige) und Tempo-Erkennung");
    // Click-Track 185 BPM, 10 s
    auto clicks = [] (double sr, double bpm, double secs)
    {
        std::vector<float> x ((size_t) (sr * secs), 0.0f);
        const double period = 60.0 / bpm * sr;
        for (int k = 0; k * period < x.size(); ++k)
            for (int i = 0; i < (int) (0.03 * sr); ++i)
            {
                const size_t at = (size_t) std::lround (k * period) + (size_t) i;
                if (at < x.size()) x[at] += (float) (0.8 * std::exp (-i / (0.006 * sr)) * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / sr));
            }
        return x;
    };
    for (double bpm : { 145.0, 185.0, 214.0 })
    {
        double conf = 0;
        const double t = LiveAnalyzer::estimateTempo (clicks (48000.0, bpm, 10.0), 48000.0, conf);
        std::printf ("  Tempo-Erkennung Klick %.0f BPM: %.2f BPM (Konfidenz %.2f)\n", bpm, t, conf);
        CHECK (std::abs (t - bpm) < 0.15, "Tempo-Erkennung ungenau");
    }

    // Sidechain im Plugin: Layout erlaubt, Audio unverändert, Tempo in der Anzeige
    SamsiBasslineProcessor p, q;
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    const bool ok = p.setBusesLayout (layout);
    CHECK (ok && p.getTotalNumInputChannels() == 2, "Sidechain-Layout nicht unterstützt");
    p.liveTap.setEnabled (true);
    LiveAnalyzer an; an.prepare (48000.0);
    const auto sc = clicks (48000.0, 185.0, 8.0);
    p.prepareToPlay (48000.0, 512); q.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> bp (6, 512), bq (6, 512);
    juce::MidiBuffer m;
    double d = 0;
    for (size_t pos = 0; pos + 512 <= sc.size(); pos += 512)
    {
        bp.clear(); bq.clear();
        for (int c = 0; c < 2; ++c) bp.copyFrom (c, 0, sc.data() + pos, 512);
        p.processBlock (bp, m); q.processBlock (bq, m);
        for (int c = 0; c < 6; ++c) for (int i = 0; i < 512; ++i) d = std::max (d, (double) std::abs (bp.getSample (c, i) - bq.getSample (c, i)));
        an.drain (p.liveTap);
    }
    std::printf ("  Sidechain aktiv: Ausgänge unverändert (Abweichung %.3g), angezeigt verbunden: %s, Sidechain-Tempo %.2f BPM (Klick 185)\n",
                 d, p.isSidechainConnected() ? "ja" : "nein", an.sidechainTempo());
    CHECK (d == 0.0 && p.isSidechainConnected(), "Sidechain beeinflusst den Klang");
    CHECK (std::abs (an.sidechainTempo() - 185.0) < 0.2, "Sidechain-Tempo falsch");
}

static void testSampleDropOnWaveforms()
{
    section ("Eigene Samples in die Live-Wellenformen ziehen (Laden, Ausrichten, Auto Fit)");
    SamsiBasslineProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* se = dynamic_cast<SamsiBasslineEditor*> (ed.get());
    CHECK (se != nullptr, "kein Editor");
    if (se == nullptr) return;
    // Kick-Sample = gerenderte Synth-Kick, bewusst 6 ms verzögert und invertiert in der Datei
    std::vector<float> kick;
    {
        KickSettings k; k.endHz = 46.25f;
        KickEngine e; e.prepare (44100.0);
        e.trigger (k, 1.0f, 1.0f, nullptr);
        std::vector<float> body (22050);
        e.render (body.data(), (int) body.size(), k);
        kick.assign (265, 0.0f);   // 6 ms Stille
        for (float v : body) kick.push_back (-v);
    }
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    auto kf = tmp.getChildFile ("samsi_drop_kick.wav"), bf = tmp.getChildFile ("samsi_drop_bass.wav");
    { auto mb = encodeWav (kick, 44100.0); kf.replaceWithData (mb.getData(), mb.getSize()); }
    { auto mb = encodeWav (makeTestBassNote (44100.0, 55.0), 44100.0); bf.replaceWithData (mb.getData(), mb.getSize()); }

    auto* kv = se->getLiveView (0, 0);
    auto* bv = se->getLiveView (1, 1);
    CHECK (kv != nullptr && bv != nullptr, "Live-Ansichten fehlen");
    if (kv == nullptr || bv == nullptr) return;
    CHECK (kv->isInterestedInFileDrag ({ kf.getFullPathName() }) && ! kv->isInterestedInFileDrag ({ "/tmp/x.txt" }), "Drag-Filter falsch");
    kv->filesDropped ({ kf.getFullPathName() }, 10, 10);
    const auto s1 = p.readSettings();
    std::printf ("  Kick-Sample in KICK LIVE (Synth-Ansicht) abgelegt: %s\n", kv->getMessage().toRawUTF8());
    std::printf ("    Sample an: %s, Start %.2f ms, Verzögerung %.2f ms, invertiert %s (Datei: 6 ms später, invertiert)\n",
                 s1.kick.smpOn ? "ja" : "nein", s1.kick.smpStartMs, s1.kick.smpOffsetMs, s1.kick.smpInvert ? "ja" : "nein");
    CHECK (p.getKickSample() != nullptr && s1.kick.smpOn && s1.kick.smpInvert && std::abs (s1.kick.smpStartMs - 6.0f) < 0.15f,
           "Kick-Sample nicht geladen oder nicht ausgerichtet");

    // Auto Fit abwarten (wurde durch das Ablegen gestartet; der Editor übernimmt das Ergebnis)
    bool got = false;
    const auto t0 = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - t0 < 120000)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        p.pollFitResult();
        if (p.getLastFit() != nullptr && ! p.analysis.isFitRunning()) { got = true; break; }
    }
    std::printf ("    automatisches Auto Fit nach dem Ablegen: %s\n", got ? "Ergebnis übernommen" : "KEIN Ergebnis");
    CHECK (got, "Auto Fit nach dem Ablegen nicht gestartet");

    bv->filesDropped ({ bf.getFullPathName() }, 10, 10);
    auto bs = p.getBassSample();
    std::printf ("  Bass-Sample in BASS LIVE (Analyzer-Ansicht) abgelegt: %s\n    Pegel %.0f %%\n", bv->getMessage().toRawUTF8(),
                 p.readSettings().bass.smpLevel * 100.0f);
    CHECK (bs != nullptr && std::abs (bs->pitchHz - 55.0f) < 1.5f && p.readSettings().bass.smpLevel > 0.5f, "Bass-Sample nicht geladen");
    while (juce::Time::getMillisecondCounter() - t0 < 240000 && p.analysis.isFitRunning())
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    p.pollFitResult();
    ed.reset();
    kf.deleteFile(); bf.deleteFile();
}

static void testSampleModulation()
{
    section ("Samples mit Kick- und Bass-Reglern modulieren");
    const double sr = 48000.0;
    // Test-Sample: konstanter Sinus 100 Hz, 1 s (Grundton wird erkannt)
    auto smp = std::make_shared<KickSample>();
    smp->rate = sr; smp->pitchHz = 100.0f;
    smp->data.resize ((size_t) sr);
    for (size_t i = 0; i < smp->data.size(); ++i) smp->data[i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * (double) i / sr));

    auto layer = [&] (KickSettings k)
    {
        // nur die Sample-Schicht: gesamt minus Synth allein
        k.smpOn = true; k.smpLengthMs = 900.0f;
        KickEngine a, b; a.prepare (sr); b.prepare (sr);
        KickSettings k0 = k; k0.smpOn = false;
        a.trigger (k, 1.0f, 1.0f, smp.get()); b.trigger (k0, 1.0f, 1.0f, nullptr);
        std::vector<float> x ((size_t) sr / 2), y ((size_t) sr / 2);
        a.render (x.data(), (int) x.size(), k); b.render (y.data(), (int) y.size(), k0);
        for (size_t i = 0; i < x.size(); ++i) x[i] -= y[i];
        return x;
    };
    auto zcHz = [&] (const std::vector<float>& x, double fromMs, double toMs)
    {
        int zc = 0;
        for (size_t i = (size_t) (fromMs * 48) + 1; i < (size_t) (toMs * 48); ++i) if ((x[i - 1] <= 0.0f) != (x[i] <= 0.0f)) ++zc;
        return zc * 0.5 / ((toMs - fromMs) * 0.001);
    };
    KickSettings k;  k.endHz = 50.0f; k.startHz = 400.0f; k.pitchMs = 60.0f; k.decayMs = 150.0f; k.tailMs = 40.0f; k.driveDb = 0.0f;
    const auto plain = layer (k);
    KickSettings kp = k; kp.smpPitchEnv = 1.0f;
    const auto pitched = layer (kp);
    KickSettings ka = k; ka.smpAmpEnv = 1.0f;
    const auto amped = layer (ka);
    KickSettings kk = k; kk.smpKeyTrack = true;
    const auto keyed = layer (kk);
    const double fPlain = detectPitch (plain, sr, 480, 9600, 30.0, 400.0).hz;
    const double fKey = detectPitch (keyed, sr, 2400, 9600, 30.0, 400.0).hz;
    std::printf ("  Kick-Sample (Sinus 100 Hz): ohne Modulation %.1f Hz; mit Pitch-Hüllk. 100 %%: %.0f Hz (0-10 ms), %.0f Hz (200-300 ms)\n",
                 fPlain, zcHz (pitched, 0.5, 10), zcHz (pitched, 200, 300));
    std::printf ("  Amp-Hüllk. 100 %%: Pegel bei 300-400 ms %.4f (ohne %.4f); Grundton folgen (Kick 50 Hz): %.2f Hz\n",
                 rms (amped, 14400, 19200), rms (plain, 14400, 19200), fKey);
    CHECK (std::abs (fPlain - 100.0) < 1.0, "Sample ohne Modulation verändert");
    CHECK (zcHz (pitched, 0.5, 10) > 300.0 && std::abs (zcHz (pitched, 200, 300) - 100.0) < 5.0, "Pitch-Hüllkurve wirkt nicht aufs Sample");
    CHECK (rms (amped, 14400, 19200) < 0.01 * rms (plain, 14400, 19200), "Amp-Hüllkurve wirkt nicht aufs Sample");
    CHECK (std::abs (fKey - 50.0) < 0.5, "Grundton-Folgen falsch");
    {
        KickSettings kd = k; kd.driveDb = 18.0f; KickSettings kd2 = kd; kd2.smpDrive = true;
        const double d = maxAbsDiff (layer (kd), layer (kd2));
        std::printf ("  Sample durch Sättigung: Unterschied zur unverzerrten Schicht %.3f\n", d);
        CHECK (d > 0.05, "Sättigungs-Routing wirkt nicht");
    }
    {
        // Ausrichtung mit modulierter Schicht: identische Synth-Kick als Sample, verschoben -> wird wiedergefunden
        KickSettings km = k; km.smpPitchEnv = 0.5f; km.smpAmpEnv = 0.5f; km.smpOffsetMs = 4.0f;
        KickSample ks; ks.rate = sr;
        KickEngine e; e.prepare (sr); KickSettings k0 = k; e.trigger (k0, 1.0f, 1.0f, nullptr);
        ks.data.resize ((size_t) sr / 2); e.render (ks.data.data(), (int) ks.data.size(), k0);
        const auto r = alignKickSample (km, ks, sr);
        std::printf ("  Ausrichtung mit modulierter Sample-Schicht: Korrelation %.2f -> %.2f, Verzögerung %.2f ms\n", r.coherenceBefore, r.coherenceAfter, r.delayMs);
        CHECK (r.valid && r.coherenceAfter > r.coherenceBefore, "Ausrichtung mit Modulation verbessert nicht");
    }

    // Bass: durch Filter an/aus, Pitch-Env über die Filter-Hüllkurve
    SamsiBasslineProcessor p;
    p.loadBassSampleFromMemory (encodeWav (makeTestBassNote (44100.0, 55.0, 1.0), 44100.0), "b.wav");
    setParam (p, pid::bSaw, 0.0f); setParam (p, pid::bSub, 0.0f); setParam (p, pid::bsLevel, 1.0f);
    setParam (p, pid::bCutoff, 60.0f); setParam (p, pid::bEnvAmt, 0.0f); setParam (p, pid::bKeytrack, 0.0f);
    setParam (p, pid::bSus, 1.0f); setParam (p, pid::bDec, 2000.0f); setParam (p, pid::bDrive, 0.0f);
    PatternData pat = PatternData::makeEmpty();
    { Step s; s.bass = true; s.lenPct = 300; pat.set (1, s); }
    p.setPattern (pat);
    auto hi = [&] (const Out& o) { return energyAbove (o.ch[4], 7000, 30000, 48000.0, 400.0); };
    const auto thru = run (p, 48000.0, 36000, fixedBlock (512), 120.0);
    setParam (p, pid::bsThru, 0.0f);
    const auto direct = run (p, 48000.0, 36000, fixedBlock (512), 120.0);
    std::printf ("  Bass-Sample, Cutoff 60 Hz: Energie > 400 Hz durch Filter %.3g, am Filter vorbei %.3g\n", hi (thru), hi (direct));
    CHECK (hi (direct) > 20.0 * hi (thru), "Bass-Sample-Routing (durch Filter) wirkt nicht");
    setParam (p, pid::bsThru, 1.0f); setParam (p, pid::bCutoff, 16000.0f);
    setParam (p, pid::bFDec, 400.0f); setParam (p, pid::bsPitchEnv, 12.0f);
    const auto penv = run (p, 48000.0, 36000, fixedBlock (512), 120.0);
    const int ns = 6000;   // Note beginnt bei 1/16 = 6000 Samples (120 BPM)
    const auto early = detectPitch (penv.ch[4], 48000.0, ns + 50, 2600, 50.0, 400.0);
    setParam (p, pid::bsPitchEnv, 0.0f);
    const auto flat = run (p, 48000.0, 36000, fixedBlock (512), 120.0);
    const auto late = detectPitch (flat.ch[4], 48000.0, ns + 50, 4800, 30.0, 400.0);
    std::printf ("  Bass-Sample Pitch-Env +12 st (Filter-Hüllk. 400 ms): Anfang %.1f Hz, ohne Pitch-Env %.1f Hz (Note F#0 = 46,25)\n", early.hz, late.hz);
    CHECK (early.hz > 65.0 && std::abs (late.hz - 46.25) < 1.5, "Pitch-Env auf Bass-Sample wirkt nicht");
}

static void testGlue()
{
    section ("Kick→Bass-Glue: Sidechain-Kompressor und dynamischer EQ");
    const double sr = 48000.0, bpm = 145.0, spq = 60.0 / bpm * sr;
    // langer, durchgehender Bass (eine Note je Viertel auf Schritt 0, 300 %) + Kick auf jedem Viertel
    PatternData pat = PatternData::makeEmpty();
    for (int b = 0; b < 4; ++b) { Step s; s.kick = true; s.bass = true; s.lenPct = 400; pat.set (b * 4, s); }
    auto render = [&] (std::function<void (SamsiBasslineProcessor&)> cfg)
    {
        SamsiBasslineProcessor p;
        setParam (p, pid::bSus, 1.0f); setParam (p, pid::bDec, 2000.0f); setParam (p, pid::bRel, 1.0f);
        setParam (p, pid::bCutoff, 3000.0f);
        setParam (p, pid::kDecay, 120.0f); setParam (p, pid::kTailMs, 20.0f);   // Kick klingt vor Viertelende aus
        p.setPattern (pat);
        cfg (p);
        return run (p, sr, (int) (spq * 8), fixedBlock (512), bpm);
    };
    const auto off = render ([] (SamsiBasslineProcessor&) {});
    const auto onC = render ([] (SamsiBasslineProcessor& p) { setParam (p, pid::gOn, 1.0f); setParam (p, pid::gcThr, -30.0f);
                                                             setParam (p, pid::gcRange, 6.0f); setParam (p, pid::gcRatio, 10.0f); setParam (p, pid::gcRel, 60.0f); });
    // Kick-Beginn im 5. Viertel
    const size_t k0 = (size_t) std::lround (spq * 4);
    auto db = [] (double a, double b) { return 20.0 * std::log10 (std::max (1.0e-9, a) / std::max (1.0e-9, b)); };
    const double during = db (rms (onC.ch[4], k0 + 240, k0 + 2400), rms (off.ch[4], k0 + 240, k0 + 2400));
    const double later  = db (rms (onC.ch[4], k0 + (size_t) (spq * 0.75), k0 + (size_t) spq - 50), rms (off.ch[4], k0 + (size_t) (spq * 0.75), k0 + (size_t) spq - 50));
    double dn = 0;
    for (size_t i = 0; i < onC.size(); ++i) for (int c = 0; c < 2; ++c) dn = std::max (dn, (double) std::abs (onC.ch[c][i] - (onC.ch[2 + c][i] + onC.ch[4 + c][i])));
    std::printf ("  Kompressor (Thr -30, Ratio 10, Range 6 dB, Release 60 ms): Bass während der Kick %.2f dB, am Ende des Viertels %.2f dB; Nulltest MIX = KICK + BASS: %.2g\n",
                 during, later, dn);
    CHECK (during < -3.0 && during > -6.3 && std::abs (later) < 0.5, "Sidechain-Kompression falsch");
    CHECK (dn == 0.0, "Glue verletzt MIX = KICK + BASS");
    CHECK (maxAbsDiff (off.ch[2], onC.ch[2]) == 0.0, "Glue verändert die Kick");

    // Dynamischer EQ: Band 1 auf 46 Hz, -9 dB, Kompressor aus
    const auto onEq = render ([] (SamsiBasslineProcessor& p) { setParam (p, pid::gOn, 1.0f); setParam (p, pid::gcOn, 0.0f);
                                                              setParam (p, pid::d1On, 1.0f); setParam (p, pid::d1Freq, 46.25f); setParam (p, pid::d1Q, 1.0f);
                                                              setParam (p, pid::d1Range, -9.0f); setParam (p, pid::d1Thr, -40.0f); });
    auto bandRms = [&] (const std::vector<float>& x, size_t a, size_t b, double lo, double hi)
    {
        // FFT-Bandenergie (Hann, auf Zweierpotenz aufgefüllt)
        int order = 1; while ((1 << order) < (int) (b - a)) ++order;
        const int N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (size_t i = a; i < b; ++i)
            buf[i - a] = x[i] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) (i - a) / (double) (b - a - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double e = 0;
        for (int k = 1; k < N / 2; ++k) { const double f = k * sr / N; if (f >= lo && f < hi) e += (double) buf[(size_t) k] * buf[(size_t) k]; }
        return std::sqrt (e);
    };
    const double lowCut = db (bandRms (onEq.ch[4], k0 + 480, k0 + 4800, 35, 60), bandRms (off.ch[4], k0 + 480, k0 + 4800, 35, 60));
    const double highCut = db (bandRms (onEq.ch[4], k0 + 480, k0 + 4800, 400, 2000), bandRms (off.ch[4], k0 + 480, k0 + 4800, 400, 2000));
    std::printf ("  Dyn. EQ Band 1 (46 Hz, Range -9 dB): Bass 35–60 Hz während der Kick %.2f dB, 400–2000 Hz %.2f dB\n", lowCut, highCut);
    CHECK (lowCut < -3.0 && std::abs (highCut) < 0.6, "Dynamischer EQ wirkt nicht bandbegrenzt");

    // ohne Kick: dyn. EQ und Kompressor neutral (bitgleich)
    const auto noKickOff = render ([] (SamsiBasslineProcessor& p) { setParam (p, pid::kLevel, -48.0f); });
    const auto noKickOn = render ([] (SamsiBasslineProcessor& p) { setParam (p, pid::kLevel, -48.0f); setParam (p, pid::gOn, 1.0f);
                                                                  setParam (p, pid::d1On, 1.0f); setParam (p, pid::d2On, 1.0f); });
    std::printf ("  Kick sehr leise (-48 dB): Abweichung Bass mit/ohne Glue %.2g (Thresholds nicht erreicht)\n", maxAbsDiff (noKickOff.ch[4], noKickOn.ch[4]));
    CHECK (maxAbsDiff (noKickOff.ch[4], noKickOn.ch[4]) < 1.0e-6, "Glue ohne Auslösung nicht neutral");

    // AUTO-Einrichtung und Invalidierung
    SamsiBasslineProcessor p;
    const auto h0 = soundHash (p.readSettings(), p.pattern.snapshot(), 145.0, 48000.0, 0);
    const auto msg = p.autoSetupGlue();
    const auto s = p.readSettings();
    std::printf ("  AUTO: %s\n", msg.toRawUTF8());
    CHECK (s.glue.on && std::abs (s.glue.band[0].freqHz - s.kick.endHz) < 0.2f && std::abs (s.glue.band[1].freqHz - 2.0f * s.kick.endHz) < 0.4f,
           "Glue-AUTO falsch");
    CHECK (soundHash (s, p.pattern.snapshot(), 145.0, 48000.0, 0) != h0, "Glue-Änderung invalidiert die Analyse nicht");
}

static void screenshot (const juce::String& path)
{
    section ("Screenshot der Oberfläche");
    SamsiBasslineProcessor p;
    {
        auto layout = p.getBusesLayout();   // Sidechain an (Click-Track), um die Overlay-Anzeigen zu zeigen
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        p.setBusesLayout (layout);
    }
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (SamsiBasslineEditor::kBaseW, SamsiBasslineEditor::kBaseH);

    // Vorschau + Fit abwarten, damit die Ansicht echte Daten zeigt
    setParam (p, pid::bPhase, 160.0f);
    p.startAutoFit();
    const auto t0 = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - t0 < 60000)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        if (p.getLastFit() != nullptr && p.analysis.getPreview() != nullptr && ! p.analysis.isFitRunning()) break;
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);

    auto shoot = [&] (const juce::String& file)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (file);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        const bool ok = png.writeImageToStream (img, os);
        std::printf ("  %s: %s\n", file.toRawUTF8(), ok ? "geschrieben" : "FEHLER");
    };
    // Wiedergabe simulieren: Host spielt, Sidechain = Click-Track 145 BPM, Editor holt Live-Daten ab
    TestPlayHead ph; ph.bpm = 145.0;
    int64_t playPos = 0;
    auto play = [&] (double seconds)
    {
        auto* se = dynamic_cast<SamsiBasslineEditor*> (ed.get());
        p.setPlayHead (&ph);
        juce::AudioBuffer<float> buf (6, 512);
        juce::MidiBuffer midi;
        const double spq = 60.0 / ph.bpm * 48000.0;
        const int blocks = (int) (seconds * 48000.0 / 512.0);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i)
            {
                const double t = std::fmod ((double) (playPos + i), spq) / 48000.0;
                const float v = (float) (0.5 * std::exp (-t / 0.01) * std::sin (2.0 * juce::MathConstants<double>::pi * 70.0 * t));
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            ph.ppq = (double) playPos / spq;
            p.processBlock (buf, midi);
            playPos += 512;
            if (b % 3 == 0 && se != nullptr) se->tickForTest();
        }
        p.setPlayHead (nullptr);
    };
    play (4.0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    shoot (path);
    if (auto* se = dynamic_cast<SamsiBasslineEditor*> (ed.get()))
    {
        setParam (p, pid::uImager, 1.4f);
        setParam (p, pid::bWidth, 0.6f);
        se->showView (1);
        play (5.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        shoot (path.upToLastOccurrenceOf (".", false, false) + "_analyzer.png");
        se->showView (0);
        setParam (p, pid::uImager, 1.0f);
        setParam (p, pid::bWidth, 0.0f);
    }

    // zweite Ansicht: Holzig-Darkpsy-Preset, Sample-Layer, Wavetable, Key-Ansicht mit einem roten Fehler
    if (auto* se = dynamic_cast<SamsiBasslineEditor*> (ed.get()))
    {
        p.applyFactoryPreset (17);   // Holzig: Wurzelgeist
        p.loadKickSampleFromMemory (encodeWav (makeTestKick (44100.0, 49.0), 44100.0), "Top-Kick.wav");
        setParam (p, pid::ksOn, 1.0f);
        p.loadBassSampleFromMemory (encodeWav (makeTestBassNote (44100.0, 49.0), 44100.0), "Holz-Bass.wav");
        setParam (p, pid::bsLevel, 0.6f);
        Step st = p.pattern.getStep (13); st.note = 2; p.setStep (13, st);   // bewusst außerhalb der Skala
        setParam (p, pid::ksAmpEnv, 0.6f);
        setParam (p, pid::ksPitchEnv, 0.5f);
        se->showPages (2, 3, 1, 1);
        play (3.0);
        for (int i = 0; i < 12; ++i) { se->tickForTest(); juce::MessageManager::getInstance()->runDispatchLoopUntil (60); }
        shoot (path.upToLastOccurrenceOf (".", false, false) + "_key.png");
        p.applyFactoryPreset (23);   // High Tech, Triolen-Raster
        p.autoSetupGlue();
        se->showPages (0, 1, 0, 2);
        play (3.0);
        for (int i = 0; i < 12; ++i) { se->tickForTest(); juce::MessageManager::getInstance()->runDispatchLoopUntil (60); }
        shoot (path.upToLastOccurrenceOf (".", false, false) + "_pick.png");
    }
    ed.reset();
}

//==============================================================================
int main (int argc, char** argv)
{
    const bool onlyNew = argc > 1 && juce::String (argv[1]) == "--new";
    juce::ScopedJuceInitialiser_GUI gui;
    if (onlyNew)
    {
        testUtilityChain(); testUtilityStages(); testLimiter(); testLoudnessAndMeters(); testLiveTapAndCaptures(); testSidechainAndTempo();
        testSampleDropOnWaveforms();
        testSampleModulation();
        testGlue();
        std::printf ("\n==== %d Pruefungen, %d Fehler ====\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }

    juce::String shot;
    bool onlyShot = false;
    if (argc >= 4 && juce::String (argv[1]) == "--render")
    {
        // Rendert ein Preset (JSON) als WAV: MIX, KICK und BASS getrennt (für die Referenz-Kalibrierung)
        auto json = juce::JSON::parse (juce::File (argv[2]));
        if (! json.isObject()) { std::printf ("JSON ungueltig\n"); return 2; }
        const int bars = argc >= 5 ? juce::String (argv[4]).getIntValue() : 16;
        const double sr = 44100.0;
        SamsiBasslineProcessor p;
        for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (prm->getDefaultValue());
        if (auto* obj = json["params"].getDynamicObject())
            for (auto& prop : obj->getProperties())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (prop.name.toString())))
                    r->setValueNotifyingHost (r->convertTo0to1 ((float) (double) prop.value));
                else
                    std::printf ("unbekannter Parameter %s\n", prop.name.toString().toRawUTF8());
        PatternData pat = PatternData::makeEmpty();
        if (auto* arr = json["pattern"].getArray())
            for (int i = 0; i < juce::jmin (kNumSteps, arr->size()); ++i)
            {
                auto& e = arr->getReference (i);
                Step st;
                st.kick = (int) e[0] != 0; st.kickVel = (int) e[1]; st.bass = (int) e[2] != 0; st.bassVel = (int) e[3];
                st.note = (int) e[4]; st.lenPct = (int) e[5]; st.slide = (int) e[6] != 0;
                pat.set (i, st);
            }
        p.setPattern (pat);
        const double bpm = (double) json["bpm"];
        const int total = (int) std::lround (bars * 4 * 60.0 / bpm * sr);
        const auto o = run (p, sr, total, fixedBlock (512), bpm);
        const juce::String base = juce::String (argv[3]).upToLastOccurrenceOf (".wav", false, true);
        auto write = [&] (const juce::String& file, int chL, int chR)
        {
            std::vector<float> mono (o.size());
            for (size_t i = 0; i < o.size(); ++i) mono[i] = 0.5f * (o.ch[chL][i] + o.ch[chR][i]);
            auto mb = encodeWav (mono, sr);
            juce::File (file).replaceWithData (mb.getData(), mb.getSize());
        };
        write (base + ".wav", 0, 1);
        write (base + "_kick.wav", 2, 3);
        write (base + "_bass.wav", 4, 5);
        double pk = 0; for (int c = 0; c < 2; ++c) pk = std::max (pk, peak (o.ch[c]));
        std::printf ("OK peak %.2f dBFS\n", juce::Decibels::gainToDecibels ((float) pk));
        return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "--fit-presets")
    {
        // Auto Fit für jedes Werks-Preset bei seinem Tempo (48 kHz); Ergebnis als JSON (für gen_cpp.py)
        const auto& presets = factoryPresets();
        juce::Array<juce::var> list;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            if (presets[(size_t) i].theme == "Init" || presets[(size_t) i].theme == "Werkstatt") continue;
            SamsiBasslineProcessor p;
            p.applyFactoryPreset (i);
            const double bpm = juce::String (presets[(size_t) i].hint).getDoubleValue();
            std::atomic<bool> cancel { false };
            std::atomic<float> progress { 0.0f };
            const auto r = runAutoFit (p.readSettings(), p.pattern.snapshot(), 48000.0, bpm > 60.0 ? bpm : 190.0, cancel, progress);
            auto* o = new juce::DynamicObject();
            auto metr = [] (const FitMetrics& m)
            {
                auto* d = new juce::DynamicObject();
                d->setProperty ("cancel_worst_20_60", m.cancelWorstDb[0]);
                d->setProperty ("cancel_mean_20_60", m.cancelMeanDb[0]);
                d->setProperty ("cancel_mean_60_120", m.cancelMeanDb[1]);
                d->setProperty ("onset_cancel", m.onsetCancelDb);
                d->setProperty ("attack_loss", m.kickAttackLossDb);
                d->setProperty ("sum_peak", m.sumPeakDb);
                d->setProperty ("bass_energy", m.bassEnergyDb);
                d->setProperty ("loudness", m.loudnessLu);
                d->setProperty ("cost", m.cost);
                return juce::var (d);
            };
            o->setProperty ("name", juce::String::fromUTF8 (presets[(size_t) i].name.c_str()));
            o->setProperty ("ok", r.ok);
            o->setProperty ("before", metr (r.before));
            o->setProperty ("after", metr (r.after));
            auto* f = new juce::DynamicObject();
            f->setProperty ("fit_phase", r.best.phaseDeg);
            f->setProperty ("fit_polarity", r.best.polarity ? 1 : 0);
            f->setProperty ("fit_offset_ms", r.best.offsetMs);
            f->setProperty ("fit_duck_db", r.best.duckDb);
            f->setProperty ("fit_duck_ms", r.best.duckMs);
            f->setProperty ("fit_trim_db", r.best.trimDb);
            f->setProperty ("fit_bass_att_add", r.best.attackAddMs);
            f->setProperty ("fit_kick_tail", r.best.kickTailPct);
            f->setProperty ("fit_phase_mode", r.best.phaseMode == PhaseMode::FollowKick ? 1 : 0);
            o->setProperty ("fit", juce::var (f));
            list.add (juce::var (o));
            std::printf ("%-26s Ausloeschung 20-60 Hz max %.2f -> %.2f dB, Zielfunktion %.2f -> %.2f\n", presets[(size_t) i].name.c_str(),
                         r.before.cancelWorstDb[0], r.after.cancelWorstDb[0], r.before.cost, r.after.cost);
        }
        juce::File (argv[2]).replaceWithText (juce::JSON::toString (juce::var (list)));
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--list-params")
    {
        // Parameterliste als Markdown-Tabelle (für die Dokumentation)
        SamsiBasslineProcessor p;
        std::printf ("| ID | Name | Bereich | Standard |\n|---|---|---|---|\n");
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                const auto& range = r->getNormalisableRange();
                juce::String rng;
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r)) rng = c->choices.joinIntoString (" / ");
                else if (dynamic_cast<juce::AudioParameterBool*> (r) != nullptr) rng = "aus / an";
                else rng = r->getText (0.0f, 32) + " ... " + r->getText (1.0f, 32);
                juce::ignoreUnused (range);
                std::printf ("| `%s` | %s | %s | %s |\n", r->getParameterID().toRawUTF8(), r->getName (64).toRawUTF8(),
                             rng.toRawUTF8(), r->getText (r->getDefaultValue(), 32).toRawUTF8());
            }
        return 0;
    }
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--screenshot" && i + 1 < argc) shot = argv[++i];
        if (juce::String (argv[i]) == "--only-screenshot") onlyShot = true;
    }

    if (! onlyShot)
    {
        testAutomationParameterCount();
        testNullTest();
        testBlockSizes();
        testTriggerCounts();
        testTransportStop();
        testMidi();
        testStateRecall();
        testInstances();
        testRealtimeVsOffline();
        testExtremes();
        testSmoothing();
        testFitWorkflow();
        testKickSampleLayer();
        testLayerSwapWhilePlaying();
        testWavetables();
        testPickDamper();
        testKeyAnalysis();
        testPresets();
        testGateModes();
        testLookahead();
        testGrids();
        testBassSample();
        testKickAlign();
        testFactoryPresetSet();
        testUtilityChain();
        testUtilityStages();
        testLimiter();
        testLoudnessAndMeters();
        testLiveTapAndCaptures();
        testSidechainAndTempo();
        testSampleDropOnWaveforms();
        testSampleModulation();
        testGlue();
        testAutoFit();
    }
    if (shot.isNotEmpty()) screenshot (shot);

    std::printf ("\n==== %d Pruefungen, %d Fehler ====\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
