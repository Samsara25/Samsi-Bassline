#include "SamsiEngine.h"

#include <cmath>

namespace samsi
{

BassSettings effectiveBass (const Settings& s) noexcept
{
    BassSettings b = s.bass;
    if (s.fit.enabled)
        b.attackMs = sanitize (b.attackMs + s.fit.attackAddMs, 0.05f, 200.0f, 0.5f);
    return b;
}

float effectiveKickTail (const Settings& s) noexcept
{
    return s.fit.enabled ? sanitize (s.fit.kickTailPct * 0.01f, 0.3f, 1.0f, 1.0f) : 1.0f;
}

double bassShiftMs (const Settings& s) noexcept
{
    double ms = sanitize (s.bass.grooveDelayMs, -20.0f, 20.0f, 0.0f);
    if (s.fit.enabled)
        ms += sanitize (s.fit.offsetMs, -10.0f, 10.0f, 0.0f);
    return ms;
}

//==============================================================================
void SamsiEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0;
    maxBlock = std::max (16, maxBlockSize);

    for (auto* b : { &kickBuf, &bassLBuf, &bassRBuf, &mixLBuf, &mixRBuf })
        b->assign ((size_t) maxBlock, 0.0f);
    flagBuf.assign ((size_t) maxBlock, 0);

    kickEngine.prepare (sr);
    bassEngine.prepare (sr);
    glue.prepare (sr);
    for (auto* sm : { &kickLevel, &master, &abGain })
        sm->prepare (sr, 25.0);
    busDrive.prepare (sr, 25.0);
    reset();
}

void SamsiEngine::reset() noexcept
{
    kickEngine.reset();
    bassEngine.reset();
    glue.reset();
    clock = 0;
    wasPlaying = false;
    anchorPpq = 0.0;
    anchorBpm = 0.0;
    anchorSample = 0;
    lastStep = -1;
    numHeld = 0;
    numPending = 0;
    numGateHeld = 0;
    gateRoot = -1;
    internalClock = false;
    numGateChanges = 0;
    kickLevel.reset (1.0f);
    master.reset (1.0f);
    abGain.reset (1.0f);
    busDrive.reset ((float) driveDbToFactor (0.0));
    busClipL.reset();
    busClipR.reset();
    primed = false;
}

int SamsiEngine::lookaheadSamples (const Settings& s, double sampleRate) noexcept
{
    const int ms = std::min (50, std::max (0, s.global.lookaheadMs));
    return ms > 0 ? msToSamples ((double) ms, sampleRate) : 0;
}

void SamsiEngine::process (const Settings& s, const PatternData& pattern, const TransportInfo& t,
                           const MidiEvt* midi, int numMidi, int numSamples, const Layers& lay) noexcept
{
    static const Layers empty {};
    layers = &lay;
    if (layers == nullptr) layers = &empty;
    kickEngine.validateSample (layers->kickSample);
    // Blöcke, die größer sind als vorbereitet, werden in Teilstücken verarbeitet.
    // Ausgabepuffer haben nur maxBlock Länge -> Aufrufer (Prozessor) zerteilt große Blöcke selbst.
    numSamples = std::min (numSamples, maxBlock);
    processChunk (s, pattern, t, midi, numMidi, 0, numSamples);
}

void SamsiEngine::handleMidiBass (bool on, int note, int vel, int sample, double glide) noexcept
{
    EngineEvent e;
    e.sample = sample;
    e.source = 1;

    if (on)
    {
        // Duplikate entfernen, oben auf den Stapel legen
        int w = 0;
        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[(size_t) i] != note) heldNotes[(size_t) w++] = heldNotes[(size_t) i];
        numHeld = w;
        const bool legato = numHeld > 0 && bassEngine.currentGate() && bassEngine.currentSource() == 1;
        if (numHeld < (int) heldNotes.size()) heldNotes[(size_t) numHeld++] = note;

        lastMidiVel = vel;
        e.type = EngineEvent::BassOn;
        e.note = note;
        e.velocity = (float) vel / 127.0f;
        e.slide = legato && glide > 0.0;
        e.id = note;
        events.add (e);
    }
    else
    {
        int w = 0;
        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[(size_t) i] != note) heldNotes[(size_t) w++] = heldNotes[(size_t) i];
        numHeld = w;

        if (numHeld > 0 && bassEngine.currentSource() == 1 && bassEngine.currentId() == note)
        {
            // zurück zur zuletzt gehaltenen Note (gebunden)
            e.type = EngineEvent::BassOn;
            e.note = heldNotes[(size_t) numHeld - 1];
            e.velocity = (float) lastMidiVel / 127.0f;
            e.slide = true;
            e.id = e.note;
        }
        else
        {
            e.type = EngineEvent::BassOff;
            e.id = note;
        }
        events.add (e);
    }
}

void SamsiEngine::applyEvent (const EngineEvent& e, const Settings& s, const KickSettings& kEff,
                              const BassSettings& bEff, float tailScale) noexcept
{
    switch (e.type)
    {
        case EngineEvent::KickOn:
            kickEngine.trigger (kEff, e.velocity, tailScale, layers != nullptr ? layers->kickSample : nullptr);
            bassEngine.triggerDuck (s.fit.enabled ? s.fit.duckDb : 0.0f, s.fit.duckMs);
            if (e.sample >= 0 && e.sample < (int) flagBuf.size()) flagBuf[(size_t) e.sample] |= 1;
            if (triggerLog != nullptr) triggerLog->kickOn.push_back (clock + e.sample);
            break;

        case EngineEvent::BassOn:
        {
            double phaseDeg = (double) sanitize (s.bass.startPhaseDeg, 0.0f, 360.0f, 0.0f);
            if (s.fit.enabled)
                phaseDeg += (double) sanitize (s.fit.phaseDeg, 0.0f, 360.0f, 0.0f);
            double phase01 = phaseDeg / 360.0;

            // Kausaler Echtzeitmodus: Startphase an die momentane Kick-Phase koppeln.
            // Funktioniert auch für Live-MIDI, weil die Kick in derselben Instanz läuft.
            if (s.fit.enabled && s.fit.phaseMode == PhaseMode::FollowKick && kickEngine.currentActive())
                phase01 += kickEngine.currentPhase();

            bassEngine.noteOn (e.note, e.velocity, e.slide, e.source, e.id, wrap01 (phase01), bEff);
            if (e.sample >= 0 && e.sample < (int) flagBuf.size()) flagBuf[(size_t) e.sample] |= (uint8_t) (e.slide ? 4 : 2);
            if (triggerLog != nullptr && ! e.slide)
            {
                triggerLog->bassOn.push_back (clock + e.sample);
                if (triggerLog->bassNotes.size() < triggerLog->bassNotes.capacity()) triggerLog->bassNotes.push_back (e.note);
            }
            break;
        }

        case EngineEvent::BassOff:
            bassEngine.noteOff (e.source, e.id);
            if (triggerLog != nullptr) triggerLog->bassOff.push_back (clock + e.sample);
            break;

        case EngineEvent::AllBassOff:
            bassEngine.allNotesOff();
            numHeld = 0;
            break;
    }
}

void SamsiEngine::processChunk (const Settings& s, const PatternData& pattern, const TransportInfo& tIn,
                                const MidiEvt* midi, int numMidi, int /*offset*/, int n) noexcept
{
    if (n <= 0) return;
    events.clear();
    std::fill (flagBuf.begin(), flagBuf.begin() + std::min (n, (int) flagBuf.size()), (uint8_t) 0);

    const double bpm = sanitize ((float) tIn.bpm, 20.0f, 999.0f, 145.0f);
    const double samplesPerQn = 60.0 / bpm * sr;

    const int mode = std::min (2, std::max (0, s.global.seqMode));
    const bool gateMode = mode != 0;
    const int L = lookaheadSamples (s, sr);
    const int shiftS = (int) std::lround (bassShiftMs (s) * 0.001 * sr);
    // Mit Lookahead läuft alles um L Samples verzögert (Host gleicht die gemeldete Latenz aus).
    // Dadurch ist ein negativer Bass-Versatz bis -L auch bei Live-MIDI möglich.
    const int bassDelay = std::max (0, L + shiftS);
    const int kickNote = s.global.midiKickNote;

    //--- 1. Eingehendes MIDI in die Warteschlange --------------------------------
    for (int i = 0; i < std::min (numMidi, kMaxMidiPerBlock); ++i)
    {
        const auto& m = midi[i];
        const int smp = std::min (n - 1, std::max (0, m.sample));
        const bool on = m.noteOn && m.velocity > 0;
        const int delay = (m.note == kickNote || gateMode) ? L : bassDelay;
        if (numPending < (int) pending.size())
            pending[(size_t) numPending++] = { clock + smp + delay, on, m.note, m.velocity };
    }

    //--- 2. Fällige Ereignisse entnehmen (nach Zeit sortiert) ----------------------
    std::array<Pending, 256> due;
    int numDue = 0;
    {
        int w = 0;
        for (int i = 0; i < numPending; ++i)
        {
            const auto p = pending[(size_t) i];
            if (p.due < clock + n && numDue < (int) due.size())
                due[(size_t) numDue++] = p;
            else
                pending[(size_t) w++] = p;
        }
        numPending = w;
        for (int i = 1; i < numDue; ++i)
        {
            const auto e = due[(size_t) i];
            int j = i - 1;
            while (j >= 0 && due[(size_t) j].due > e.due) { due[(size_t) j + 1] = due[(size_t) j]; --j; }
            due[(size_t) j + 1] = e;
        }
    }

    //--- 3. MIDI auswerten ----------------------------------------------------------
    const bool gateAtStart = numGateHeld > 0;
    const int rootAtStart = gateRoot;
    numGateChanges = 0;

    auto rootFor = [&s] (int heldNote) noexcept
    {
        // Tonhöhenklasse der gehaltenen Note, Oktave möglichst nah am Grundton-Regler
        const int base = s.global.rootNote;
        int diff = ((heldNote % 12) - (base % 12) + 12) % 12;
        if (diff > 6) diff -= 12;
        return std::min (127, std::max (0, base + diff));
    };

    for (int i = 0; i < numDue; ++i)
    {
        const auto& d = due[(size_t) i];
        const int ls = (int) std::max<int64_t> (0, d.due - clock);

        if (d.note == kickNote)
        {
            if (d.on)
            {
                EngineEvent e; e.sample = ls; e.type = EngineEvent::KickOn;
                e.velocity = (float) d.vel / 127.0f; e.source = 1;
                events.add (e);
            }
            continue;
        }

        if (gateMode)
        {
            int w = 0;
            for (int k = 0; k < numGateHeld; ++k)
                if (gateHeld[(size_t) k] != d.note) gateHeld[(size_t) w++] = gateHeld[(size_t) k];
            numGateHeld = w;
            if (d.on && numGateHeld < (int) gateHeld.size())
                gateHeld[(size_t) numGateHeld++] = d.note;
            gateRoot = numGateHeld > 0 ? rootFor (gateHeld[(size_t) numGateHeld - 1]) : gateRoot;
            if (numGateChanges < (int) gateChanges.size())
                gateChanges[(size_t) numGateChanges++] = { ls, numGateHeld > 0, gateRoot };
        }
        else
        {
            handleMidiBass (d.on, d.note, d.vel, ls, s.bass.glideMs);
        }
    }

    auto gateAt = [&] (int sample, int& root) noexcept
    {
        bool open = gateAtStart;
        root = rootAtStart;
        for (int i = 0; i < numGateChanges; ++i)
            if (gateChanges[(size_t) i].sample <= sample) { open = gateChanges[(size_t) i].open; root = gateChanges[(size_t) i].root; }
        return open;
    };

    //--- 4. Zeitbasis (Host-Transport oder interne Uhr im Gate-Modus) ------------------
    const bool hostPlaying = tIn.playing && tIn.hasPpq && std::isfinite (tIn.ppq);
    const double hostPpq = tIn.ppq - (double) L / samplesPerQn;
    bool timelineActive = false;

    if (hostPlaying)
    {
        internalClock = false;
        if (wasPlaying)
        {
            // erwartete Position aus dem Anker (mit dem Tempo, das bisher galt)
            const double spqOld = 60.0 / anchorBpm * sr;
            const double expected = anchorPpq + (double) (clock - anchorSample) / spqOld;
            const double tolQn = 4.0 / samplesPerQn;   // ±4 Samples gelten als kontinuierlich
            if (std::abs (hostPpq - expected) > tolQn)
            {
                EngineEvent e; e.sample = 0; e.type = EngineEvent::AllBassOff;  // Loop-Sprung / Positionswechsel
                events.add (e);
                anchorPpq = hostPpq; anchorSample = clock;
            }
            else if (std::abs (bpm - anchorBpm) > 1.0e-9)
            {
                anchorPpq = expected; anchorSample = clock;                    // Tempowechsel: kontinuierlich neu verankern
            }
        }
        else
        {
            anchorPpq = hostPpq; anchorSample = clock;                         // Transport-Start
        }
        anchorBpm = bpm;
        timelineActive = true;
    }
    else
    {
        if (wasPlaying)
        {
            EngineEvent e; e.sample = 0; e.type = EngineEvent::AllBassOff;
            events.add (e);
        }

        if (gateMode)
        {
            // interne Uhr: startet mit Schritt 1, sobald das Gate öffnet (unabhängig vom Host-Transport)
            if (! internalClock)
            {
                int openAt = gateAtStart ? 0 : -1;
                for (int i = 0; i < numGateChanges && openAt < 0; ++i)
                    if (gateChanges[(size_t) i].open) openAt = gateChanges[(size_t) i].sample;
                if (openAt >= 0)
                {
                    internalClock = true;
                    anchorPpq = 0.0; anchorSample = clock + openAt; anchorBpm = bpm;
                }
            }
            else if (std::abs (bpm - anchorBpm) > 1.0e-9 && clock >= anchorSample)
            {
                anchorPpq += (double) (clock - anchorSample) / (60.0 / anchorBpm * sr);
                anchorSample = clock; anchorBpm = bpm;
            }
            timelineActive = internalClock;
        }
        else
            internalClock = false;
    }
    wasPlaying = hostPlaying;

    //--- 5. Sequencer-Ereignisse (im Gate-Modus gefiltert und transponiert) ------------
    if (timelineActive && s.global.seqOn)
    {
        const double shiftQn = bassShiftMs (s) * 0.001 * bpm / 60.0;
        seqEvents.clear();
        Sequencer::collect (pattern, s.global, anchorPpq, anchorSample, clock, samplesPerQn, n, shiftQn, seqEvents);
        for (int i = 0; i < seqEvents.size(); ++i)
        {
            EngineEvent e = seqEvents[i];
            if (gateMode && e.type != EngineEvent::BassOff)
            {
                int root = -1;
                if (! gateAt (e.sample, root)) continue;
                if (mode == 2 && root >= 0 && e.type == EngineEvent::BassOn)
                    e.note = std::min (127, std::max (0, e.note + (root - s.global.rootNote)));
            }
            events.add (e);
        }
    }

    if (gateMode)
        for (int i = 0; i < numGateChanges; ++i)
            if (! gateChanges[(size_t) i].open)
            {
                EngineEvent e; e.sample = gateChanges[(size_t) i].sample; e.type = EngineEvent::AllBassOff;
                events.add (e);
            }

    // interne Uhr anhalten, wenn das Gate am Blockende geschlossen ist (nächstes Öffnen startet bei Schritt 1)
    if (internalClock && numGateHeld == 0)
        internalClock = false;

    tlActive = timelineActive;
    tlAnchorPpq = anchorPpq; tlAnchorSample = anchorSample; tlSpq = samplesPerQn; tlChunkClock = clock;

    if (timelineActive && clock >= anchorSample)
    {
        const double ppqNow = anchorPpq + (double) (clock - anchorSample) / samplesPerQn;
        const int grid = std::min (2, std::max (0, s.global.seqGrid));
        const int ns = stepsForGrid (grid);
        const auto stepIdx = (int64_t) std::floor (ppqNow / stepQnForGrid (grid));
        lastStep = (int) (((stepIdx % ns) + ns) % ns);
    }
    else
        lastStep = -1;

    events.sort();

    //--- Rendern zwischen den Ereignissen ------------------------------------
    const KickSettings kEff = s.kick;
    const BassSettings bEff = effectiveBass (s);
    const float tailScale = effectiveKickTail (s);
    const int tableIdx = std::min (kNumTableSlots - 1, std::max (0, s.bass.wtTable));
    const WavetableData* wtable = layers != nullptr ? layers->tables[(size_t) tableIdx] : nullptr;

    int pos = 0;
    for (int i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        const int at = std::min (n, std::max (pos, e.sample));
        if (at > pos)
        {
            kickEngine.render (kickBuf.data() + pos, at - pos, kEff);
            bassEngine.render (bassLBuf.data() + pos, bassRBuf.data() + pos, at - pos, bEff, s.fit, wtable, layers != nullptr ? layers->bassSample : nullptr);
            pos = at;
        }
        applyEvent (e, s, kEff, bEff, tailScale);
    }
    if (pos < n)
    {
        kickEngine.render (kickBuf.data() + pos, n - pos, kEff);
        bassEngine.render (bassLBuf.data() + pos, bassRBuf.data() + pos, n - pos, bEff, s.fit, wtable, layers != nullptr ? layers->bassSample : nullptr);
    }

    //--- Pegel, A/B-Lautheitsausgleich, Master, Summe -------------------------
    kickLevel.setTarget (dbToGain (sanitize (s.kick.levelDb, -100.0f, 12.0f, 0.0f)));
    master.setTarget (dbToGain (sanitize (s.global.masterDb, -100.0f, 12.0f, 0.0f)));
    const bool abActive = ! s.fit.enabled && s.global.loudMatch;
    abGain.setTarget (abActive ? dbToGain (sanitize (s.global.abGainDb, -12.0f, 12.0f, 0.0f)) : 1.0f);
    busDrive.setTarget ((float) driveDbToFactor (sanitize (s.global.busClipDriveDb, 0.0f, 24.0f, 0.0f)));
    if (! primed)
    {
        // Start ohne Rampe: sonst wäre der erste Schlag nach dem Laden kurz zu laut
        kickLevel.reset (dbToGain (sanitize (s.kick.levelDb, -100.0f, 12.0f, 0.0f)));
        master.reset (dbToGain (sanitize (s.global.masterDb, -100.0f, 12.0f, 0.0f)));
        abGain.reset (abActive ? dbToGain (sanitize (s.global.abGainDb, -12.0f, 12.0f, 0.0f)) : 1.0f);
        busDrive.reset ((float) driveDbToFactor (sanitize (s.global.busClipDriveDb, 0.0f, 24.0f, 0.0f)));
        primed = true;
    }

    const bool glueOn = s.glue.on;
    if (glueOn) glue.update (s.glue);
    bool finite = true;
    for (int i = 0; i < n; ++i)
    {
        const float common = master.next() * abGain.next();   // gleich für alle Busse -> MIX = KICK + BASS bleibt exakt
        const float k  = kickBuf[(size_t) i] * kickLevel.next() * common;
        float bl = bassLBuf[(size_t) i] * common;
        float br = bassRBuf[(size_t) i] * common;
        if (glueOn) glue.process (k, bl, br, s.glue);   // Kick steuert Kompressor/dyn. EQ des Basses
        kickBuf[(size_t) i]  = k;
        bassLBuf[(size_t) i] = bl;
        bassRBuf[(size_t) i] = br;

        float ml = k + bl, mr = k + br;
        if (s.global.monitor == 1)      { ml = k;  mr = k;  }   // Solo betrifft nur den MIX-Ausgang
        else if (s.global.monitor == 2) { ml = bl; mr = br; }
        const double bd = busDrive.next();
        const float cl = (float) busClipL.process (ml, bd);   // Zustand immer mitführen
        const float cr = (float) busClipR.process (mr, bd);
        if (s.global.busClipOn) { ml = cl; mr = cr; }
        mixLBuf[(size_t) i] = ml;
        mixRBuf[(size_t) i] = mr;

        finite = finite && std::isfinite (ml) && std::isfinite (mr) && std::abs (ml) < 1.0e4f && std::abs (mr) < 1.0e4f;
    }

    if (! finite)
    {
        // Schutz: ungültige Werte niemals an den Host geben
        for (auto* b : { &kickBuf, &bassLBuf, &bassRBuf, &mixLBuf, &mixRBuf })
            std::fill (b->begin(), b->begin() + n, 0.0f);
        kickEngine.reset();
        bassEngine.reset();
        glue.reset();
        busClipL.reset();
        busClipR.reset();
        ++nanResets;
    }

    clock += n;
}

} // namespace samsi
