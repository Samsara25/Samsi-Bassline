#include "SampleLoader.h"

#include "PitchDetect.h"

#include <cstring>

namespace samsi
{

DecodedAudio decodeAudio (const juce::MemoryBlock& bytes, double maxSeconds)
{
    DecodedAudio out;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto stream = std::make_unique<juce::MemoryInputStream> (bytes, false);
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::move (stream)));
    if (reader == nullptr)
    {
        out.error = juce::String::fromUTF8 ("Format nicht erkannt (unterstützt: WAV, AIFF, FLAC, OGG, MP3).");
        return out;
    }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        out.error = "Datei ist leer.";
        return out;
    }

    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
    const int ch = (int) juce::jlimit (1u, 8u, reader->numChannels);
    juce::AudioBuffer<float> buf (ch, len);
    reader->read (&buf, 0, len, 0, true, true);

    out.rate = reader->sampleRate;
    out.mono.assign ((size_t) len, 0.0f);
    for (int c = 0; c < ch; ++c)
    {
        const float* src = buf.getReadPointer (c);
        for (int i = 0; i < len; ++i) out.mono[(size_t) i] += src[i] / (float) ch;
    }
    for (auto& v : out.mono)
        if (! std::isfinite (v)) v = 0.0f;
    return out;
}

std::shared_ptr<KickSample> makeKickSample (const DecodedAudio& a, const juce::String& name)
{
    auto k = std::make_shared<KickSample>();
    k->data = a.mono;
    k->rate = a.rate;
    k->name = name.toStdString();
    const auto p = detectKickPitch (k->data, k->rate);
    k->pitchHz = (float) p.hz;
    k->pitchConfidence = (float) p.confidence;
    uint64_t h = 1469598103934665603ull;
    for (float f : k->data)
    {
        uint32_t b;
        std::memcpy (&b, &f, sizeof b);
        h = (h ^ b) * 1099511628211ull;
    }
    k->id = h ^ (uint64_t) a.rate;
    return k;
}

std::shared_ptr<KickSample> makeBassSample (const DecodedAudio& a, const juce::String& name)
{
    auto k = makeKickSample (a, name);
    // Grundton im tonalen Teil: nach den ersten 20 ms, bis zu 0,5 s
    const int start = (int) (0.02 * a.rate);
    const int len = std::min ((int) a.mono.size() - start, (int) (0.5 * a.rate));
    const auto p = detectPitch (a.mono, a.rate, start, len, 25.0, 1000.0);
    k->pitchHz = (float) p.hz;
    k->pitchConfidence = (float) p.confidence;
    k->id ^= 0x5bd1e995u;
    return k;
}

juce::MemoryBlock encodeWav (const std::vector<float>& mono, double rate)
{
    juce::MemoryBlock mb;
    {
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::MemoryOutputStream> (mb, false);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer != nullptr)
        {
            const float* ptr = mono.data();
            writer->writeFromFloatArrays (&ptr, 1, (int) mono.size());
        }
    }
    return mb;
}

} // namespace samsi
