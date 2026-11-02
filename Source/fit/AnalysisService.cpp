#include "AnalysisService.h"

#include <juce_dsp/juce_dsp.h>

namespace samsi
{

AnalysisService::AnalysisService() : juce::Thread ("Samsi Analysis")
{
    startThread (juce::Thread::Priority::low);
}

AnalysisService::~AnalysisService()
{
    fitCancel.store (true);
    signalThreadShouldExit();
    wake.signal();
    stopThread (10000);
}

void AnalysisService::requestPreview (const Settings& s, const PatternData& p, double sr, double bpm, uint64_t hash,
                                      const LayerRefs& layers)
{
    {
        const juce::ScopedLock sl (lock);
        previewJob = { true, s, p, sr, bpm, hash, layers };
    }
    wake.signal();
}

void AnalysisService::requestFit (const Settings& s, const PatternData& p, double sr, double bpm, uint64_t hash,
                                  const LayerRefs& layers)
{
    {
        const juce::ScopedLock sl (lock);
        fitJob = { true, s, p, sr, bpm, hash, layers };
    }
    fitCancel.store (true);    // laufenden Fit abbrechen, neuer startet
    fitRunning.store (true);
    fitProgress.store (0.0f);
    wake.signal();
}

void AnalysisService::cancelFit()
{
    {
        const juce::ScopedLock sl (lock);
        fitJob.valid = false;
    }
    fitCancel.store (true);
}

std::shared_ptr<const PreviewData> AnalysisService::getPreview() const
{
    const juce::ScopedLock sl (lock);
    return preview;
}

std::shared_ptr<const FitResult> AnalysisService::takeFitResult()
{
    const juce::ScopedLock sl (lock);
    auto r = fitResult;
    fitResult.reset();
    return r;
}

std::shared_ptr<PreviewData> AnalysisService::computePreview (const Settings& s, const PatternData& p, double sr, double bpm,
                                                             uint64_t hash, const LayerRefs& layers)
{
    auto pd = std::make_shared<PreviewData>();
    pd->hash = hash;
    pd->render = renderPattern (s, p, sr, bpm, layers.raw());
    pd->metrics = analyze (pd->render, {}, {}, true);

    // Spektrum (Welch: 8192er Hann-Fenster, 50 % Überlappung)
    constexpr int order = 13, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (size - 1));

    const auto& r = pd->render;
    const std::vector<float>* sig[3] = { &r.kick, &r.bass, &r.mix };
    std::array<std::vector<double>, 3> power;
    std::vector<float> buf ((size_t) size * 2);

    for (int c = 0; c < 3; ++c)
    {
        power[(size_t) c].assign ((size_t) size / 2 + 1, 0.0);
        int segments = 0;
        const auto& x = *sig[c];
        for (int start = 0; start + size <= (int) x.size() || (start == 0 && ! x.empty()); start += size / 2)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < size && start + i < (int) x.size(); ++i)
                buf[(size_t) i] = x[(size_t) (start + i)] * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k <= size / 2; ++k)
                power[(size_t) c][(size_t) k] += (double) buf[(size_t) k] * (double) buf[(size_t) k];
            ++segments;
            if (start + size >= (int) x.size()) break;
        }
        for (auto& v : power[(size_t) c]) v /= std::max (1, segments);
    }

    constexpr int points = 160;
    const double fLo = 20.0, fHi = 2000.0;
    double ref = 1.0e-20;
    for (int i = 0; i < points; ++i)
    {
        const double f = fLo * std::pow (fHi / fLo, (double) i / (points - 1));
        pd->spectrumFreqs.push_back ((float) f);
    }
    for (int c = 0; c < 3; ++c)
    {
        auto& out = pd->spectrumDb[(size_t) c];
        for (float f : pd->spectrumFreqs)
        {
            const double bin = (double) f * size / sr;
            const int b0 = std::max (0, std::min (size / 2 - 1, (int) bin));
            const double fr = bin - b0;
            const double pw = power[(size_t) c][(size_t) b0] * (1.0 - fr) + power[(size_t) c][(size_t) b0 + 1] * fr;
            out.push_back ((float) pw);
            ref = std::max (ref, pw);
        }
    }
    for (auto& ch : pd->spectrumDb)
        for (auto& v : ch)
            v = (float) (10.0 * std::log10 (std::max (1.0e-20, (double) v) / ref));
    return pd;
}

void AnalysisService::run()
{
    while (! threadShouldExit())
    {
        wake.wait (200);
        if (threadShouldExit()) break;

        Job fj, pj;
        {
            const juce::ScopedLock sl (lock);
            fj = fitJob;   fitJob.valid = false;
            pj = previewJob; previewJob.valid = false;
        }

        if (fj.valid)
        {
            fitCancel.store (false);
            fitRunning.store (true);
            auto r = std::make_shared<FitResult> (runAutoFit (fj.settings, fj.pattern, fj.sr, fj.bpm, fitCancel, fitProgress, fj.layers.raw()));
            r->hash = fj.hash;
            {
                const juce::ScopedLock sl (lock);
                if (r->ok) fitResult = r;
                if (! fitJob.valid) fitRunning.store (false);
            }
        }
        else
        {
            const juce::ScopedLock sl (lock);
            if (! fitJob.valid) fitRunning.store (false);
        }

        if (pj.valid && ! threadShouldExit())
        {
            auto p = computePreview (pj.settings, pj.pattern, pj.sr, pj.bpm, pj.hash, pj.layers);
            const juce::ScopedLock sl (lock);
            preview = p;
        }
    }
}

} // namespace samsi
