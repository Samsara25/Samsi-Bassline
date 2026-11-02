#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "content/Presets.h"
#include "content/SampleLoader.h"
#include "content/WavetableFactory.h"

using namespace samsi;

namespace
{
    constexpr int kMidiCapacity = 2048;
    constexpr double kMaxKickSeconds = 10.0;
    constexpr double kMaxTableSeconds = 60.0;
    constexpr size_t kMaxEmbedBytes = 8u * 1024u * 1024u;
}

juce::AudioProcessor::BusesProperties SamsiBasslineProcessor::makeBuses()
{
    return BusesProperties()
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)   // nur Anzeige, standardmäßig aus
        .withOutput ("Mix",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Kick", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Bass", juce::AudioChannelSet::stereo(), true);
}

SamsiBasslineProcessor::SamsiBasslineProcessor()
    : juce::AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "SAMSI_BASSLINE", createParameterLayout()),
      refs (apvts)
{
    midiScratch.resize (kMidiCapacity);
    for (auto& p : peaks) p.store (0.0f);

    // eingebaute Wavetables hier (Message-Thread) erzeugen, nie im Audio-Thread
    const auto& b = builtinWavetables();
    for (size_t i = 0; i < b.size(); ++i) builtinRaw[i] = b[i].get();

    startTimerHz (10);
}

SamsiBasslineProcessor::~SamsiBasslineProcessor()
{
    stopTimer();
}

//==============================================================================
bool SamsiBasslineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
            return false;   // Sidechain: aus, mono oder stereo

    for (int i = 0; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (i == 0 && set.isDisabled()) return false;   // Mix muss existieren
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo() && set != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

int SamsiBasslineProcessor::computeLatency (const Settings& s) const noexcept
{
    const double sr = getAnalysisSampleRate();
    Settings tmp;
    tmp.global.lookaheadMs = s.global.lookaheadMs;
    return SamsiEngine::lookaheadSamples (tmp, sr)
           + BusUtility::baseLatencySamples (s.utility, sr)
           + (s.utility.limiterOn ? Limiter::latencySamples (sr) : 0);
}

void SamsiBasslineProcessor::updateLookahead()
{
    const auto s = readSettings();
    const double sr = getAnalysisSampleRate();
    Settings tmp;
    tmp.global.lookaheadMs = s.global.lookaheadMs;
    const int samples = SamsiEngine::lookaheadSamples (tmp, sr);
    activeLookaheadMs.store (s.global.lookaheadMs);
    activeLookahead.store (samples);
    activeUtilBase.store (BusUtility::baseLatencySamples (s.utility, sr));
    activeLimiter.store (s.utility.limiterOn);
    const int total = computeLatency (s);
    if (getLatencySamples() != total)
        setLatencySamples (total);   // Host gleicht die Verzögerung aus (alle drei Ausgänge gleich)
}

void SamsiBasslineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = std::max (32, samplesPerBlock);
    engine.prepare (sampleRate, preparedBlockSize);
    lastSampleRate.store (sampleRate);
    for (auto& u : utility) u.prepare (sampleRate, preparedBlockSize);
    limiter.prepare (sampleRate);
    for (auto& d : limAlign) d.prepare (Limiter::latencySamples (sampleRate));
    for (int b = 0; b < 3; ++b) { chL[b].assign ((size_t) preparedBlockSize, 0.0f); chR[b].assign ((size_t) preparedBlockSize, 0.0f); }
    scL.assign ((size_t) preparedBlockSize, 0.0f);
    scR.assign ((size_t) preparedBlockSize, 0.0f);
    tapScratch.assign ((size_t) preparedBlockSize, {});
    {
        const auto s0 = readSettings();
        for (auto& u : utility) u.reset (s0.utility);
        utilWasOn = s0.utility.on;
        limWasOn = s0.utility.limiterOn;
    }
    updateLookahead();   // ohne Lookahead: Latenz 0 (ADAA statt Oversampling)
}

void SamsiBasslineProcessor::releaseResources() {}

Settings SamsiBasslineProcessor::readSettings() const noexcept
{
    auto s = refs.read();
    s.global.abGainDb = abGainDb.load();
    return s;
}

//==============================================================================
void SamsiBasslineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) { midi.clear(); blocksDone.fetch_add (1); return; }
    // Hinweis: Eingangs- und Ausgangskanäle teilen sich den Puffer. Der Sidechain wird je
    // Teilstück gelesen, bevor dieses Teilstück überschrieben wird.

    Settings s = readSettings();
    s.global.lookaheadMs = activeLookaheadMs.load();   // muss zur gemeldeten Latenz passen
    const PatternData pat = pattern.snapshot();

    Layers layers;
    layers.kickSample = kickSlot.forAudio();
    layers.bassSample = bassSlot.forAudio();
    for (size_t i = 0; i < builtinRaw.size(); ++i) layers.tables[i] = builtinRaw[i];
    layers.tables[(size_t) kUserTableSlot] = tableSlot.forAudio();

    //--- Transport ---------------------------------------------------------
    TransportInfo t;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) t.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { t.ppq = *ppq; t.hasPpq = true; }
        }
    }
    if (! std::isfinite (t.bpm) || t.bpm < 20.0 || t.bpm > 999.0) t.bpm = 145.0;
    lastBpm.store (t.bpm);
    hostPlaying.store (t.playing);

    //--- MIDI in feste Liste --------------------------------------------------
    int numMidi = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (numMidi >= kMidiCapacity) break;
        if (msg.isNoteOn())
            midiScratch[(size_t) numMidi++] = { meta.samplePosition, true, msg.getNoteNumber(), (int) msg.getVelocity() };
        else if (msg.isNoteOff())
            midiScratch[(size_t) numMidi++] = { meta.samplePosition, false, msg.getNoteNumber(), 0 };
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (int n = 0; n < 128 && numMidi < kMidiCapacity; ++n)
                midiScratch[(size_t) numMidi++] = { meta.samplePosition, false, n, 0 };
    }
    midi.clear();   // kein MIDI-Ausgang

    //--- Busse --------------------------------------------------------------
    auto scBus   = getBusCount (true) > 0 && getBus (true, 0) != nullptr && getBus (true, 0)->isEnabled()
                       ? getBusBuffer (buffer, true, 0) : juce::AudioBuffer<float>();
    sidechainActive.store (scBus.getNumChannels() > 0);
    auto mixBus  = getBusBuffer (buffer, false, 0);
    auto kickBus = getBusCount (false) > 1 ? getBusBuffer (buffer, false, 1) : juce::AudioBuffer<float>();
    auto bassBus = getBusCount (false) > 2 ? getBusBuffer (buffer, false, 2) : juce::AudioBuffer<float>();

    auto writeBus = [] (juce::AudioBuffer<float>& bus, int offset, int n, const float* l, const float* r)
    {
        const int ch = bus.getNumChannels();
        if (ch <= 0) return;
        if (ch == 1)
        {
            auto* d = bus.getWritePointer (0, offset);
            for (int i = 0; i < n; ++i) d[i] = 0.5f * (l[i] + r[i]);
        }
        else
        {
            std::copy (l, l + n, bus.getWritePointer (0, offset));
            std::copy (r, r + n, bus.getWritePointer (1, offset));
        }
    };

    float pk[3] = { 0.0f, 0.0f, 0.0f };
    const double spq = 60.0 / t.bpm * getAnalysisSampleRate();
    int midiIndex = 0;

    for (int offset = 0; offset < numSamples; offset += preparedBlockSize)
    {
        const int n = std::min (preparedBlockSize, numSamples - offset);

        TransportInfo tc = t;
        tc.ppq = t.ppq + (double) offset / spq;

        // MIDI-Ereignisse dieses Teilstücks auf lokale Positionen umrechnen
        int first = midiIndex, count = 0;
        while (midiIndex < numMidi && midiScratch[(size_t) midiIndex].sample < offset + n)
        {
            midiScratch[(size_t) midiIndex].sample -= offset;
            ++midiIndex; ++count;
        }

        // Sidechain dieses Teilstücks sichern (teilt sich Kanäle mit den Ausgängen)
        if (scBus.getNumChannels() > 0)
        {
            std::copy (scBus.getReadPointer (0, offset), scBus.getReadPointer (0, offset) + n, scL.data());
            const int c1 = scBus.getNumChannels() > 1 ? 1 : 0;
            std::copy (scBus.getReadPointer (c1, offset), scBus.getReadPointer (c1, offset) + n, scR.data());
        }
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, offset, n);

        engine.process (s, pat, tc, midiScratch.data() + first, count, n, layers);
        if (s.glue.on)
        {
            auto& gl = engine.getGlueMutable();
            float prev = glueGr.load();
            while (gl.maxCompGrDb > prev && ! glueGr.compare_exchange_weak (prev, gl.maxCompGrDb)) {}
            gl.maxCompGrDb = 0.0f;
            glueBand[0].store (gl.bandGainDb[0]);
            glueBand[1].store (gl.bandGainDb[1]);
        }

        // Engine-Ausgänge in Arbeitspuffer
        const float* k = engine.kick();
        std::copy (engine.mixL(), engine.mixL() + n, chL[0].data());
        std::copy (engine.mixR(), engine.mixR() + n, chR[0].data());
        std::copy (k, k + n, chL[1].data());
        std::copy (k, k + n, chR[1].data());
        std::copy (engine.bassL(), engine.bassL() + n, chL[2].data());
        std::copy (engine.bassR(), engine.bassR() + n, chR[2].data());

        const bool tapOn = liveTap.isEnabled();
        auto fillTap = [&] (bool postStage)
        {
            if (! tapOn || postStage != s.utility.post) return;
            const uint8_t* fl = engine.triggerFlags();
            const bool sc = scBus.getNumChannels() > 0;
            for (int i = 0; i < n; ++i)
            {
                auto& f = tapScratch[(size_t) i];
                f.mixL = chL[0][(size_t) i]; f.mixR = chR[0][(size_t) i];
                f.kick = 0.5f * (chL[1][(size_t) i] + chR[1][(size_t) i]);
                f.bass = 0.5f * (chL[2][(size_t) i] + chR[2][(size_t) i]);
                f.scL = sc ? scL[(size_t) i] : 0.0f; f.scR = sc ? scR[(size_t) i] : 0.0f;
                const double ppq = engine.ppqAt (i);
                f.beat = std::isfinite (ppq) ? (float) (ppq - std::floor (ppq)) : -1.0f;
                f.flags = fl[i];
            }
        };
        fillTap (false);

        // Ausgangs-Utility: gleiche lineare Kette auf allen drei Bussen
        const int base = activeUtilBase.load();
        if (s.utility.on != utilWasOn)
        {
            for (auto& u : utility) u.reset (s.utility);
            utilWasOn = s.utility.on;
        }
        if (s.utility.on)
            for (int b = 0; b < 3; ++b)
                utility[(size_t) b].process (chL[b].data(), chR[b].data(), n, s.utility, base);

        // Limiter nur auf MIX (nichtlinear, gekennzeichnet); KICK/BASS um dieselbe Latenz verzögert
        const bool lim = activeLimiter.load();
        if (lim != limWasOn) { limiter.reset(); for (auto& d : limAlign) d.reset(); limWasOn = lim; }
        if (lim)
        {
            limiter.process (chL[0].data(), chR[0].data(), n, s.utility.limiterCeilDb);
            for (int i = 0; i < n; ++i)
            {
                chL[1][(size_t) i] = limAlign[0].tick (chL[1][(size_t) i]);
                chR[1][(size_t) i] = limAlign[1].tick (chR[1][(size_t) i]);
                chL[2][(size_t) i] = limAlign[2].tick (chL[2][(size_t) i]);
                chR[2][(size_t) i] = limAlign[3].tick (chR[2][(size_t) i]);
            }
            limiterGr.store (limiter.getGainReductionDb());
        }
        else
            limiterGr.store (0.0f);

        fillTap (true);
        if (tapOn) liveTap.push (tapScratch.data(), n);

        writeBus (mixBus, offset, n, chL[0].data(), chR[0].data());
        if (kickBus.getNumChannels() > 0) writeBus (kickBus, offset, n, chL[1].data(), chR[1].data());
        if (bassBus.getNumChannels() > 0) writeBus (bassBus, offset, n, chL[2].data(), chR[2].data());

        for (int i = 0; i < n; ++i)
        {
            pk[0] = std::max (pk[0], std::max (std::abs (chL[0][(size_t) i]), std::abs (chR[0][(size_t) i])));
            pk[1] = std::max (pk[1], std::max (std::abs (chL[1][(size_t) i]), std::abs (chR[1][(size_t) i])));
            pk[2] = std::max (pk[2], std::max (std::abs (chL[2][(size_t) i]), std::abs (chR[2][(size_t) i])));
        }
    }

    currentStep.store (engine.currentStep());
    gateOpen.store (engine.isGateOpen());
    for (size_t b = 0; b < 3; ++b)
    {
        float prev = peaks[b].load();
        while (pk[b] > prev && ! peaks[b].compare_exchange_weak (prev, pk[b])) {}
    }
    blocksDone.fetch_add (1, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessorEditor* SamsiBasslineProcessor::createEditor()
{
    return new SamsiBasslineEditor (*this);
}

void SamsiBasslineProcessor::timerCallback()
{
    collectGarbage();

    const auto s = readSettings();
    if (s.global.lookaheadMs != activeLookaheadMs.load()
        || BusUtility::baseLatencySamples (s.utility, getAnalysisSampleRate()) != activeUtilBase.load()
        || s.utility.limiterOn != activeLimiter.load())
        updateLookahead();
}

void SamsiBasslineProcessor::collectGarbage()
{
    const auto done = blocksDone.load (std::memory_order_acquire);
    kickSlot.collect (done);
    bassSlot.collect (done);
    tableSlot.collect (done);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> SamsiBasslineProcessor::createStateXml()
{
    auto state = apvts.copyState();

    juce::ValueTree pat ("PATTERN");
    const auto snap = pattern.snapshot();
    juce::StringArray steps;
    for (auto v : snap.steps) steps.add (juce::String::toHexString ((juce::int64) v));
    pat.setProperty ("steps", steps.joinIntoString (","), nullptr);
    state.removeChild (state.getChildWithName ("PATTERN"), nullptr);
    state.appendChild (pat, nullptr);

    juce::ValueTree fit ("FITINFO");
    fit.setProperty ("abGainDb", (double) abGainDb.load(), nullptr);
    fit.setProperty ("hash", juce::String::toHexString ((juce::int64) lastFitHash), nullptr);
    state.removeChild (state.getChildWithName ("FITINFO"), nullptr);
    state.appendChild (fit, nullptr);

    // Sample und Wavetable werden eingebettet -> Projekt bleibt auf jedem Rechner vollständig
    for (auto* name : { "KICKSAMPLE", "WAVETABLE", "BASSSAMPLE" })
        state.removeChild (state.getChildWithName (name), nullptr);
    {
        const juce::ScopedLock sl (fileLock);
        if (kickFileBytes.getSize() > 0)
        {
            juce::ValueTree k ("KICKSAMPLE");
            k.setProperty ("name", kickFileName, nullptr);
            k.setProperty ("data", kickFileBytes.toBase64Encoding(), nullptr);
            state.appendChild (k, nullptr);
        }
        if (bassFileBytes.getSize() > 0)
        {
            juce::ValueTree b ("BASSSAMPLE");
            b.setProperty ("name", bassFileName, nullptr);
            b.setProperty ("data", bassFileBytes.toBase64Encoding(), nullptr);
            state.appendChild (b, nullptr);
        }
        if (tableFileBytes.getSize() > 0)
        {
            juce::ValueTree w ("WAVETABLE");
            w.setProperty ("name", tableFileName, nullptr);
            w.setProperty ("data", tableFileBytes.toBase64Encoding(), nullptr);
            state.appendChild (w, nullptr);
        }
    }

    state.setProperty ("pluginVersion", SAMSI_VERSION_STRING, nullptr);
    state.setProperty ("preset", currentPreset.load(), nullptr);
    return state.createXml();
}

bool SamsiBasslineProcessor::applyStateXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType()))
        return false;   // ungültige Daten: aktuelle (sichere) Einstellungen behalten

    auto tree = juce::ValueTree::fromXml (xml);
    const juce::ScopedValueSetter<bool> svs (restoringState, true);

    const auto pat = tree.getChildWithName ("PATTERN");
    if (pat.isValid())
    {
        juce::StringArray steps;
        steps.addTokens (pat.getProperty ("steps").toString(), ",", "");
        if (steps.size() == 16 || steps.size() == kNumSteps)   // ältere Projekte: 16 Schritte
        {
            PatternData p = PatternData::makeEmpty();
            for (int i = 0; i < steps.size(); ++i)
                p.steps[(size_t) i] = Step::unpack ((uint32_t) steps[i].getHexValue64()).pack();   // validiert
            pattern.setAll (p);
        }
    }

    const auto fit = tree.getChildWithName ("FITINFO");
    if (fit.isValid())
    {
        const double g = (double) fit.getProperty ("abGainDb", 0.0);
        abGainDb.store (std::isfinite (g) ? (float) juce::jlimit (-12.0, 12.0, g) : 0.0f);
        lastFitHash = (uint64_t) fit.getProperty ("hash").toString().getHexValue64();
    }

    const auto ks = tree.getChildWithName ("KICKSAMPLE");
    if (ks.isValid())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (ks.getProperty ("data").toString()))
            loadKickSampleFromMemory (mb, ks.getProperty ("name").toString());
    }
    else if (kickSlot.get() != nullptr)
        clearKickSample();

    const auto wt = tree.getChildWithName ("WAVETABLE");
    if (wt.isValid())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (wt.getProperty ("data").toString()))
            loadWavetableFromMemory (mb, wt.getProperty ("name").toString());
    }
    else if (tableSlot.get() != nullptr)
        clearWavetable();

    const auto bsmp = tree.getChildWithName ("BASSSAMPLE");
    if (bsmp.isValid())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (bsmp.getProperty ("data").toString()))
            loadBassSampleFromMemory (mb, bsmp.getProperty ("name").toString());
    }
    else if (bassSlot.get() != nullptr)
        clearBassSample();

    for (auto* name : { "KICKSAMPLE", "WAVETABLE", "BASSSAMPLE" })
        tree.removeChild (tree.getChildWithName (name), nullptr);   // große Daten nicht im Parameterbaum halten

    currentPreset.store (juce::jlimit (0, juce::jmax (0, getNumPresets() - 1), (int) tree.getProperty ("preset", 0)));
    apvts.replaceState (tree);
    return true;
}

void SamsiBasslineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateXml())
        copyXmlToBinary (*xml, destData);
}

void SamsiBasslineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

//==============================================================================
int SamsiBasslineProcessor::getNumPresets() const
{
    return (int) factoryPresets().size();
}

juce::String SamsiBasslineProcessor::getPresetName (int index) const
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? juce::String::fromUTF8 (p[(size_t) index].name.c_str()) : juce::String();
}

void SamsiBasslineProcessor::setParamPlain (const char* id, float plain)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void SamsiBasslineProcessor::applyFactoryPreset (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    const auto& pr = presets[(size_t) index];

    // nicht übernommen: Lookahead (Latenz) und Monitor – das sind Arbeitsplatz-Einstellungen
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->getParameterID();
            if (id == pid::lookahead || id == pid::monitor || id == pid::seqMode
                || id == pid::ksStart || id == pid::ksOffset || id == pid::ksInvert || id == pid::ksTune)
                continue;   // Ausrichtung eines geladenen Kick-Samples bleibt erhalten
            r->beginChangeGesture();
            r->setValueNotifyingHost (r->getDefaultValue());
            r->endChangeGesture();
        }
    for (const auto& v : pr.values)
        setParamPlain (v.first, v.second);

    setPattern (pr.pattern);
    currentPreset.store (index);
}

juce::File SamsiBasslineProcessor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Samsi Bassline").getChildFile ("Presets");
}

bool SamsiBasslineProcessor::savePresetFile (const juce::File& f)
{
    auto xml = createStateXml();
    if (xml == nullptr) return false;
    f.getParentDirectory().createDirectory();
    return xml->writeTo (f);
}

juce::String SamsiBasslineProcessor::loadPresetFile (const juce::File& f)
{
    auto xml = juce::parseXML (f);
    if (xml == nullptr) return "Datei konnte nicht gelesen werden.";
    if (! applyStateXml (*xml)) return "Keine Samsi-Bassline-Presetdatei.";
    return {};
}

//==============================================================================
juce::String SamsiBasslineProcessor::loadKickSampleFromMemory (const juce::MemoryBlock& bytes, const juce::String& name)
{
    auto audio = decodeAudio (bytes, kMaxKickSeconds);
    if (audio.error.isNotEmpty()) return audio.error;
    if (audio.mono.size() < 16) return "Sample ist zu kurz.";

    auto ks = makeKickSample (audio, name);
    {
        const juce::ScopedLock sl (fileLock);
        kickFileBytes = bytes.getSize() <= kMaxEmbedBytes ? bytes : encodeWav (audio.mono, audio.rate);
        kickFileName = name;
    }
    kickSlot.publish (std::move (ks), blocksDone.load());
    if (! restoringState) updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return {};
}

juce::String SamsiBasslineProcessor::loadKickSample (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return "Datei konnte nicht gelesen werden.";
    const auto err = loadKickSampleFromMemory (mb, f.getFileName());
    if (err.isEmpty())
        lastLoadAlign = alignKickSampleNow();   // neues Sample sofort phasenrichtig an die Synth-Kick legen
    return err;
}

juce::String SamsiBasslineProcessor::autoSetupGlue()
{
    const auto s = readSettings();
    const double endHz = juce::jlimit (25.0, 120.0, (double) s.kick.endHz);
    const double bpm = getAnalysisBpm() > 20.0 ? getAnalysisBpm() : 145.0;
    // Kick-Pegel aus der letzten Vorschau-Messung (ohne Vorschau: konservativer Standardwert)
    double kickPk = -6.0;
    if (auto pv = analysis.getPreview()) kickPk = juce::jlimit (-40.0, 0.0, pv->metrics.kickPeakDb);
    const double stepMs = 60000.0 / bpm / 4.0;
    setParamPlain (pid::gOn, 1.0f);
    setParamPlain (pid::gcOn, 1.0f);
    setParamPlain (pid::gcStyle, 0.0f);
    setParamPlain (pid::gcThr, (float) (kickPk - 14.0));
    setParamPlain (pid::gcRatio, 3.0f);
    setParamPlain (pid::gcAtt, 1.5f);
    setParamPlain (pid::gcRel, (float) juce::jlimit (20.0, 400.0, stepMs * 0.9));
    setParamPlain (pid::gcKnee, 6.0f);
    setParamPlain (pid::gcRange, 4.0f);
    setParamPlain (pid::gcScHp, 20.0f);
    setParamPlain (pid::gcScLp, (float) juce::jlimit (60.0, 400.0, endHz * 4.0));
    setParamPlain (pid::gcMix, 1.0f);
    // Band 1: Kick-Grundton im Bass leicht absenken, solange die Kick klingt
    setParamPlain (pid::d1On, 1.0f); setParamPlain (pid::d1Type, 0.0f);
    setParamPlain (pid::d1Freq, (float) endHz); setParamPlain (pid::d1Q, 1.2f); setParamPlain (pid::d1Range, -6.0f);
    setParamPlain (pid::d1Thr, (float) (kickPk - 18.0)); setParamPlain (pid::d1Att, 1.0f);
    setParamPlain (pid::d1Rel, (float) juce::jlimit (20.0, 300.0, stepMs * 0.7));
    // Band 2: erster Oberton der Kick, sanfter
    setParamPlain (pid::d2On, 1.0f); setParamPlain (pid::d2Type, 0.0f);
    setParamPlain (pid::d2Freq, (float) (endHz * 2.0)); setParamPlain (pid::d2Q, 1.5f); setParamPlain (pid::d2Range, -3.0f);
    setParamPlain (pid::d2Thr, (float) (kickPk - 22.0)); setParamPlain (pid::d2Att, 1.0f);
    setParamPlain (pid::d2Rel, (float) juce::jlimit (20.0, 300.0, stepMs * 0.6));
    return juce::String::fromUTF8 ("Glue eingerichtet: Bänder ") + juce::String (endHz, 1) + " / " + juce::String (endHz * 2.0, 1)
           + juce::String::fromUTF8 (" Hz, Kick-Pegel ") + juce::String (kickPk, 1) + " dBFS, Release " + juce::String (juce::roundToInt (stepMs * 0.9)) + " ms";
}

KickAlignResult SamsiBasslineProcessor::alignKickSampleNow()
{
    const auto k = kickSlot.get();
    if (k == nullptr) return {};
    const auto r = alignKickSample (readSettings().kick, *k, getAnalysisSampleRate());
    if (r.valid)
    {
        setParamPlain (pid::ksStart, r.startMs);
        setParamPlain (pid::ksOffset, r.delayMs);
        setParamPlain (pid::ksInvert, r.invert ? 1.0f : 0.0f);
    }
    return r;
}

juce::String SamsiBasslineProcessor::loadBassSampleFromMemory (const juce::MemoryBlock& bytes, const juce::String& name)
{
    auto audio = decodeAudio (bytes, kMaxKickSeconds);
    if (audio.error.isNotEmpty()) return audio.error;
    if (audio.mono.size() < 64) return "Sample ist zu kurz.";

    auto bs = makeBassSample (audio, name);
    {
        const juce::ScopedLock sl (fileLock);
        bassFileBytes = bytes.getSize() <= kMaxEmbedBytes ? bytes : encodeWav (audio.mono, audio.rate);
        bassFileName = name;
    }
    bassSlot.publish (std::move (bs), blocksDone.load());
    if (! restoringState) updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return {};
}

juce::String SamsiBasslineProcessor::loadBassSample (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return "Datei konnte nicht gelesen werden.";
    const auto err = loadBassSampleFromMemory (mb, f.getFileName());
    if (err.isEmpty() && readSettings().bass.smpLevel < 0.01f)
        setParamPlain (pid::bsLevel, 0.8f);
    return err;
}

void SamsiBasslineProcessor::clearBassSample()
{
    {
        const juce::ScopedLock sl (fileLock);
        bassFileBytes.reset();
        bassFileName.clear();
    }
    bassSlot.publish (nullptr, blocksDone.load());
    if (! restoringState) updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void SamsiBasslineProcessor::clearKickSample()
{
    {
        const juce::ScopedLock sl (fileLock);
        kickFileBytes.reset();
        kickFileName.clear();
    }
    kickSlot.publish (nullptr, blocksDone.load());
    if (! restoringState) updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

juce::String SamsiBasslineProcessor::loadWavetableFromMemory (const juce::MemoryBlock& bytes, const juce::String& name)
{
    auto audio = decodeAudio (bytes, kMaxTableSeconds);
    if (audio.error.isNotEmpty()) return audio.error;

    std::string info;
    auto wt = wavetableFromAudio (audio.mono, audio.rate, name.toStdString(), info);
    if (wt == nullptr) return juce::String::fromUTF8 (info.c_str());
    {
        const juce::ScopedLock sl (fileLock);
        tableFileBytes = bytes.getSize() <= kMaxEmbedBytes ? bytes : encodeWav (audio.mono, audio.rate);
        tableFileName = name;
        tableInfo = juce::String::fromUTF8 (info.c_str());
    }
    tableSlot.publish (std::move (wt), blocksDone.load());
    if (! restoringState) updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return {};
}

juce::String SamsiBasslineProcessor::loadWavetable (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return "Datei konnte nicht gelesen werden.";
    const auto err = loadWavetableFromMemory (mb, f.getFileName());
    if (err.isEmpty())
        setParamPlain (pid::wtTable, (float) kUserTableSlot);   // eigene Tabelle gleich auswählen
    return err;
}

void SamsiBasslineProcessor::clearWavetable()
{
    {
        const juce::ScopedLock sl (fileLock);
        tableFileBytes.reset();
        tableFileName.clear();
        tableInfo.clear();
    }
    tableSlot.publish (nullptr, blocksDone.load());
    if (! restoringState) updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

std::shared_ptr<const WavetableData> SamsiBasslineProcessor::getWavetableForSlot (int slot) const
{
    if (slot == kUserTableSlot) return tableSlot.get();
    const auto& b = builtinWavetables();
    return juce::isPositiveAndBelow (slot, (int) b.size()) ? b[(size_t) slot] : nullptr;
}

LayerRefs SamsiBasslineProcessor::makeLayerRefs() const
{
    LayerRefs r;
    r.kickSample = kickSlot.get();
    r.bassSample = bassSlot.get();
    const auto& b = builtinWavetables();
    for (size_t i = 0; i < b.size(); ++i) r.tables[i] = b[i];
    r.tables[(size_t) kUserTableSlot] = tableSlot.get();
    return r;
}

uint64_t SamsiBasslineProcessor::layerId() const
{
    const auto k = kickSlot.get();
    const auto w = tableSlot.get();
    const auto b = bassSlot.get();
    return (k != nullptr ? k->id : 0) * 31u + (w != nullptr ? w->id : 0) + (b != nullptr ? b->id * 7u : 0);
}

//==============================================================================
KeyReport SamsiBasslineProcessor::analyzeKey() const
{
    const auto k = kickSlot.get();
    return samsi::analyzeKey (readSettings(), pattern.snapshot(), k != nullptr ? (double) k->pitchHz : 0.0);
}

void SamsiBasslineProcessor::snapKickToKey()
{
    const auto s = readSettings();
    setParamPlain (pid::kEnd, (float) kickHzForKey (s.global.keyRoot, s.kick.endHz));
}

void SamsiBasslineProcessor::snapBassToKey()
{
    const auto s = readSettings();
    setParamPlain (pid::root, (float) bassRootForKey (s.global.keyRoot, s.global.rootNote));
}

void SamsiBasslineProcessor::snapSampleToKick()
{
    const auto k = kickSlot.get();
    if (k == nullptr || k->pitchHz <= 0.0f) return;
    const auto s = readSettings();
    setParamPlain (pid::ksTune, (float) sampleTuneForKick (k->pitchHz, s.kick.endHz, s.kick.smpTuneSt));
}

void SamsiBasslineProcessor::snapPatternToScale()
{
    const auto s = readSettings();
    setPattern (samsi::snapPatternToScale (pattern.snapshot(), s.global.rootNote, s.global.keyRoot, s.global.keyScale));
}

//==============================================================================
void SamsiBasslineProcessor::setStep (int index, const Step& st)
{
    pattern.setStep (index, st);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void SamsiBasslineProcessor::setPattern (const PatternData& p)
{
    pattern.setAll (p);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

uint64_t SamsiBasslineProcessor::currentSoundHash() const
{
    return soundHash (readSettings(), pattern.snapshot(), getAnalysisBpm(), getAnalysisSampleRate(), layerId());
}

bool SamsiBasslineProcessor::isFitStale() const
{
    return lastFitHash != 0 && currentSoundHash() != lastFitHash;
}

void SamsiBasslineProcessor::startAutoFit()
{
    const auto s = readSettings();
    const auto p = pattern.snapshot();
    analysis.requestFit (s, p, getAnalysisSampleRate(), getAnalysisBpm(),
                         soundHash (s, p, getAnalysisBpm(), getAnalysisSampleRate(), layerId()), makeLayerRefs());
}

FitSettings SamsiBasslineProcessor::currentFitParams() const
{
    return readSettings().fit;
}

void SamsiBasslineProcessor::applyFit (const FitSettings& f)
{
    setParamPlain (pid::fPhase, f.phaseDeg);
    setParamPlain (pid::fPolarity, f.polarity ? 1.0f : 0.0f);
    setParamPlain (pid::fOffset, f.offsetMs);
    setParamPlain (pid::fDuckDb, f.duckDb);
    setParamPlain (pid::fDuckMs, f.duckMs);
    setParamPlain (pid::fTrim, f.trimDb);
    setParamPlain (pid::fAttAdd, f.attackAddMs);
    setParamPlain (pid::fKickTail, f.kickTailPct);
    setParamPlain (pid::fPhaseMode, f.phaseMode == PhaseMode::FollowKick ? 1.0f : 0.0f);
    setParamPlain (pid::fEnable, 1.0f);
}

bool SamsiBasslineProcessor::pollFitResult()
{
    auto r = analysis.takeFitResult();
    if (r == nullptr) return false;

    const auto cur = readSettings();
    UndoEntry u;
    u.fit = cur.fit;
    u.hasKick = r->kickAlign.valid;
    u.ksStart = cur.kick.smpStartMs; u.ksOffset = cur.kick.smpOffsetMs; u.ksInvert = cur.kick.smpInvert;
    undoStack.push_back (u);
    if (undoStack.size() > 32) undoStack.erase (undoStack.begin());

    if (r->kickAlign.valid)
    {
        setParamPlain (pid::ksStart, r->kickAlign.startMs);
        setParamPlain (pid::ksOffset, r->kickAlign.delayMs);
        setParamPlain (pid::ksInvert, r->kickAlign.invert ? 1.0f : 0.0f);
    }
    applyFit (r->best);
    abGainDb.store ((float) r->abGainDb);
    lastFit = r;
    lastFitHash = r->hash;
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

bool SamsiBasslineProcessor::undoFit()
{
    if (undoStack.empty()) return false;
    const auto prev = undoStack.back();
    undoStack.pop_back();
    applyFit (prev.fit);
    setParamPlain (pid::fEnable, prev.fit.enabled ? 1.0f : 0.0f);
    if (prev.hasKick)
    {
        setParamPlain (pid::ksStart, prev.ksStart);
        setParamPlain (pid::ksOffset, prev.ksOffset);
        setParamPlain (pid::ksInvert, prev.ksInvert ? 1.0f : 0.0f);
    }
    return true;
}

//==============================================================================
#if ! defined (SAMSI_TEST_BUILD)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SamsiBasslineProcessor();
}
#endif
