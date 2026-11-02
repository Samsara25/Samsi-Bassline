#pragma once

#include <juce_core/juce_core.h>

#include "AutoFit.h"

#include <array>
#include <memory>

namespace samsi
{

/** Besitzende Referenzen auf Sample/Wavetable – halten die Daten während einer Analyse am Leben. */
struct LayerRefs
{
    std::shared_ptr<const KickSample> kickSample, bassSample;
    std::array<std::shared_ptr<const WavetableData>, kNumTableSlots> tables;

    Layers raw() const noexcept
    {
        Layers l;
        l.kickSample = kickSample.get();
        l.bassSample = bassSample.get();
        for (size_t i = 0; i < tables.size(); ++i) l.tables[i] = tables[i].get();
        return l;
    }
};

/** Vorschau-Daten für die Mix-/Phasenansicht: ein Takt, real gerendert. */
struct PreviewData
{
    RenderResult render;
    FitMetrics metrics;
    std::vector<float> spectrumFreqs;              // Hz, logarithmisch 20..2000
    std::array<std::vector<float>, 3> spectrumDb;  // Kick, Bass, Summe (dB, relativ)
    uint64_t hash = 0;
};

/** Hintergrund-Thread für Vorschau-Renderings und Auto Fit.
    Wird nie vom Audio-Thread benutzt; Ergebnisse holt der Message-Thread ab. */
class AnalysisService : private juce::Thread
{
public:
    AnalysisService();
    ~AnalysisService() override;

    void requestPreview (const Settings& s, const PatternData& p, double sr, double bpm, uint64_t hash, const LayerRefs& layers);
    void requestFit (const Settings& s, const PatternData& p, double sr, double bpm, uint64_t hash, const LayerRefs& layers);
    void cancelFit();

    std::shared_ptr<const PreviewData> getPreview() const;
    /** Liefert ein neues Fit-Ergebnis genau einmal (danach nullptr). */
    std::shared_ptr<const FitResult> takeFitResult();

    bool isFitRunning() const noexcept   { return fitRunning.load(); }
    float getFitProgress() const noexcept { return fitProgress.load(); }

    /** Synchrone Variante für Tests. */
    static std::shared_ptr<PreviewData> computePreview (const Settings& s, const PatternData& p, double sr, double bpm,
                                                        uint64_t hash, const LayerRefs& layers = {});

private:
    void run() override;

    struct Job
    {
        bool valid = false;
        Settings settings;
        PatternData pattern;
        double sr = 48000.0, bpm = 145.0;
        uint64_t hash = 0;
        LayerRefs layers;
    };

    mutable juce::CriticalSection lock;
    Job previewJob, fitJob;
    std::shared_ptr<const PreviewData> preview;
    std::shared_ptr<const FitResult> fitResult;
    juce::WaitableEvent wake;
    std::atomic<bool> fitRunning { false }, fitCancel { false };
    std::atomic<float> fitProgress { 0.0f };
};

} // namespace samsi
