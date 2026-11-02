#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "content/KeyAnalysis.h"
#include "dsp/SamsiEngine.h"
#include "fit/AnalysisService.h"
#include "dsp/OutputUtility.h"
#include "analysis/LiveTap.h"

#include <array>
#include <vector>

namespace samsi
{
    /** Austausch unveränderlicher Inhalte (Sample, Wavetable) ohne Sperre im Audio-Thread:
        Der Audio-Thread liest nur einen atomaren Zeiger. Alte Inhalte werden erst freigegeben,
        wenn danach mindestens zwei Audioblöcke fertig verarbeitet wurden (dann kann kein Block
        mehr den alten Zeiger halten; Kick-Sample-Stimmen mit altem Zeiger werden zu Blockbeginn beendet). */
    template <typename T>
    class LayerSlot
    {
    public:
        void publish (std::shared_ptr<const T> next, uint64_t blocksDone)
        {
            const juce::ScopedLock sl (lock);
            if (current != nullptr) retired.push_back ({ current, blocksDone });
            current = std::move (next);
            audio.store (current.get(), std::memory_order_release);
        }

        void collect (uint64_t blocksDone)
        {
            const juce::ScopedLock sl (lock);
            retired.erase (std::remove_if (retired.begin(), retired.end(),
                                           [blocksDone] (const auto& r) { return blocksDone >= r.second + 2; }),
                           retired.end());
        }

        std::shared_ptr<const T> get() const { const juce::ScopedLock sl (lock); return current; }
        const T* forAudio() const noexcept   { return audio.load (std::memory_order_acquire); }
        size_t numRetired() const            { const juce::ScopedLock sl (lock); return retired.size(); }

    private:
        mutable juce::CriticalSection lock;
        std::shared_ptr<const T> current;
        std::atomic<const T*> audio { nullptr };
        std::vector<std::pair<std::shared_ptr<const T>, uint64_t>> retired;
    };
}

/** Samsi Bassline – VST3-Instrument.
    Busse: Ausgang 1 "Mix" (Haupt), Ausgang 2 "Kick", Ausgang 3 "Bass" (je Stereo oder Mono). */
class SamsiBasslineProcessor : public juce::AudioProcessor, private juce::Timer, public juce::VST3ClientExtensions
{
public:
    SamsiBasslineProcessor();
    ~SamsiBasslineProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Samsi Bassline"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    //--- Host-Programme: bewusst nur eines. Presets laufen über die eigene Oberfläche und
    //    den gespeicherten Zustand – so kann ein Host-Programmwechsel nie einen geladenen
    //    Projektzustand überschreiben.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Samsi Bassline"; }
    void changeProgramName (int, const juce::String&) override {}

    int getNumPresets() const;
    int getCurrentPreset() const noexcept { return currentPreset.load(); }
    juce::String getPresetName (int index) const;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    samsi::PatternStore pattern;
    samsi::AnalysisService analysis;

    samsi::Settings readSettings() const noexcept;
    double getAnalysisSampleRate() const noexcept { const double s = lastSampleRate.load(); return s > 0.0 ? s : 48000.0; }
    double getAnalysisBpm() const noexcept       { return lastBpm.load(); }
    int getCurrentStep() const noexcept          { return currentStep.load(); }
    bool isHostPlaying() const noexcept          { return hostPlaying.load(); }
    bool isGateOpen() const noexcept             { return gateOpen.load(); }

    /** Pegelanzeige (Peak, linear) je Bus: 0 Mix, 1 Kick, 2 Bass */
    float takePeak (int bus) noexcept { return peaks[(size_t) bus].exchange (0.0f); }

    //--- Pattern-Bearbeitung (Message-Thread) ---------------------------------
    void setStep (int index, const samsi::Step& s);
    void setPattern (const samsi::PatternData& p);

    //--- Sample-Layer und Wavetable (Message-Thread) ---------------------------
    juce::String loadKickSample (const juce::File& f);
    juce::String loadKickSampleFromMemory (const juce::MemoryBlock& bytes, const juce::String& name);
    void clearKickSample();
    juce::String loadWavetable (const juce::File& f);
    juce::String loadWavetableFromMemory (const juce::MemoryBlock& bytes, const juce::String& name);
    void clearWavetable();
    std::shared_ptr<const samsi::KickSample> getKickSample() const    { return kickSlot.get(); }
    juce::String loadBassSample (const juce::File& f);
    juce::String loadBassSampleFromMemory (const juce::MemoryBlock& bytes, const juce::String& name);
    void clearBassSample();
    std::shared_ptr<const samsi::KickSample> getBassSample() const    { return bassSlot.get(); }
    /** Kick-Sample an der Synth-Kick ausrichten (Start/Verzögerung/Polarität). */
    samsi::KickAlignResult alignKickSampleNow();
    /** Ergebnis der automatischen Ausrichtung beim letzten Laden eines Kick-Samples */
    samsi::KickAlignResult getLastLoadAlign() const noexcept { return lastLoadAlign; }
    std::shared_ptr<const samsi::WavetableData> getUserWavetable() const { return tableSlot.get(); }
    std::shared_ptr<const samsi::WavetableData> getWavetableForSlot (int slot) const;
    juce::String getWavetableInfo() const { const juce::ScopedLock sl (fileLock); return tableInfo; }
    samsi::LayerRefs makeLayerRefs() const;
    uint64_t layerId() const;
    /** Gibt alte Inhalte frei (normalerweise vom internen Timer erledigt). */
    void collectGarbage();

    //--- Tonart-Werkzeuge (Message-Thread) ---------------------------------------
    samsi::KeyReport analyzeKey() const;
    void snapKickToKey();
    void snapBassToKey();
    void snapSampleToKick();
    void snapPatternToScale();

    //--- Presets (Message-Thread) ---------------------------------------------------
    void applyFactoryPreset (int index);
    bool savePresetFile (const juce::File& f);
    juce::String loadPresetFile (const juce::File& f);
    static juce::File getUserPresetFolder();

    //--- Auto Fit (Message-Thread) ---------------------------------------------
    void startAutoFit();
    /** Übernimmt ein fertiges Fit-Ergebnis in die Parameter (mit Undo-Eintrag). */
    bool pollFitResult();
    bool undoFit();
    bool canUndoFit() const noexcept { return ! undoStack.empty(); }
    uint64_t currentSoundHash() const;
    bool isFitStale() const;
    std::shared_ptr<const samsi::FitResult> getLastFit() const { return lastFit; }

    int getActiveLookaheadSamples() const noexcept { return activeLookahead.load(); }

    //--- Live-Anzeigen und Utility ------------------------------------------------
    /** Live-Daten für Scope/Spektrum/Meter (wird nur befüllt, solange die Oberfläche offen ist). */
    samsi::LiveTap liveTap;
    float getLimiterGainReductionDb() const noexcept { return limiterGr.load(); }
    /** Glue-Anzeige: Kompressor-Reduktion (dB, >= 0, Spitze seit letztem Abholen) und dyn. EQ-Bänder (dB) */
    float takeGlueCompGrDb() noexcept { return glueGr.exchange (0.0f); }
    float getGlueBandGainDb (int b) const noexcept { return glueBand[(size_t) juce::jlimit (0, 1, b)].load(); }
    /** Glue-Startwerte aus dem aktuellen Klang ableiten (Bänder auf Kick-Grundton/-Oberton, Thresholds
        relativ zum gemessenen Kick-Pegel, Release passend zum Tempo) und Glue einschalten. */
    juce::String autoSetupGlue();
    bool isSidechainConnected() const noexcept { return sidechainActive.load(); }

    // VST3: der Eingang ist ein Sidechain (Aux), kein Haupteingang -> bleibt ein reines Instrument
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    bool getPluginHasMainInput() const override { return false; }

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    void applyFit (const samsi::FitSettings& f);
    samsi::FitSettings currentFitParams() const;
    void setParamPlain (const char* id, float plain);
    void updateLookahead();
    int computeLatency (const samsi::Settings& s) const noexcept;
    std::unique_ptr<juce::XmlElement> createStateXml();
    bool applyStateXml (const juce::XmlElement& xml);

    samsi::ParamRefs refs;
    samsi::SamsiEngine engine;
    std::vector<samsi::MidiEvt> midiScratch;   // im prepareToPlay reserviert

    samsi::LayerSlot<samsi::KickSample> kickSlot, bassSlot;
    samsi::LayerSlot<samsi::WavetableData> tableSlot;
    std::array<const samsi::WavetableData*, samsi::kNumBuiltinTables> builtinRaw {};
    std::atomic<uint64_t> blocksDone { 0 };

    // Originaldateien für das Speichern im Projekt
    mutable juce::CriticalSection fileLock;
    juce::MemoryBlock kickFileBytes, tableFileBytes, bassFileBytes;
    juce::String kickFileName, tableFileName, tableInfo, bassFileName;

    std::atomic<double> lastSampleRate { 0.0 }, lastBpm { 145.0 };
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> hostPlaying { false }, gateOpen { false };
    std::atomic<float> abGainDb { 0.0f };
    std::array<std::atomic<float>, 3> peaks {};
    std::atomic<int> activeLookahead { 0 };     // Samples, entspricht der gemeldeten Latenz
    std::atomic<int> activeLookaheadMs { 0 };
    int preparedBlockSize = 512;

    // Ausgangs-Utility (gleiche Kette auf allen drei Bussen) + Limiter (nur MIX)
    std::array<samsi::BusUtility, 3> utility;
    samsi::Limiter limiter;
    std::array<samsi::PlainDelay, 4> limAlign;          // KICK L/R, BASS L/R: Limiter-Latenz ausgleichen
    std::vector<float> chL[3], chR[3], scL, scR;          // Arbeitspuffer je Teilstück
    std::vector<samsi::TapFrame> tapScratch;
    std::atomic<int> activeUtilBase { 0 };              // Samples, Teil der gemeldeten Latenz
    std::atomic<bool> activeLimiter { false };
    std::atomic<float> limiterGr { 0.0f };
    std::atomic<bool> sidechainActive { false };
    std::atomic<float> glueGr { 0.0f };
    std::array<std::atomic<float>, 2> glueBand {};
    bool utilWasOn = true, limWasOn = false;
    samsi::KickAlignResult lastLoadAlign;
    bool restoringState = false;

    std::atomic<int> currentPreset { 0 };

    struct UndoEntry
    {
        samsi::FitSettings fit;
        bool hasKick = false;
        float ksStart = 0.0f, ksOffset = 0.0f;
        bool ksInvert = false;
    };
    std::vector<UndoEntry> undoStack;     // nur Message-Thread
    std::shared_ptr<const samsi::FitResult> lastFit;
    uint64_t lastFitHash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamsiBasslineProcessor)
};
