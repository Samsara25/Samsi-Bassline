#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "../dsp/Layers.h"

#include <memory>

namespace samsi
{

struct DecodedAudio
{
    std::vector<float> mono;
    double rate = 0.0;
    juce::String error;
};

/** Dekodiert WAV/AIFF/FLAC/OGG/MP3 aus Speicher und mischt auf Mono. maxSeconds begrenzt die Länge. */
DecodedAudio decodeAudio (const juce::MemoryBlock& bytes, double maxSeconds);

/** Erzeugt ein Kick-Sample inkl. Grundton-Erkennung. */
std::shared_ptr<KickSample> makeKickSample (const DecodedAudio& a, const juce::String& name);

/** Erzeugt ein Bass-Sample inkl. Grundton-Erkennung (tonaler Ausschnitt, 25..1000 Hz). */
std::shared_ptr<KickSample> makeBassSample (const DecodedAudio& a, const juce::String& name);

/** Kodiert Mono-Audio als 32-Bit-Float-WAV (für Tests und zum Einbetten erzeugter Inhalte). */
juce::MemoryBlock encodeWav (const std::vector<float>& mono, double rate);

/** Unterstützte Endungen für Dateidialoge. */
inline const char* kAudioFilePatterns = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

} // namespace samsi
