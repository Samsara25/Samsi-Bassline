#include "OfflineRenderer.h"

#include <cmath>

namespace samsi
{

RenderResult renderPattern (const Settings& sIn, const PatternData& pattern, double sampleRate, double bpm,
                            const Layers& layers)
{
    Settings s = sIn;
    s.global.loudMatch = false;           // Analyse ohne A/B-Ausgleich
    s.global.seqOn = true;
    s.global.monitor = 0;                 // Analyse immer auf der Summe
    s.global.seqMode = 0;                 // Analyse am bekannten Pattern, Transport-Modus
    s.global.lookaheadMs = 0;

    RenderResult r;
    r.sampleRate = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0;
    r.bpm = bpm > 20.0 && bpm < 999.0 ? bpm : 145.0;

    const double spq = 60.0 / r.bpm * r.sampleRate;           // Samples pro Viertel
    const float tail = effectiveKickTail (s);
    const double kickMs = KickEngine::totalLengthMs (s.kick, tail);
    r.kickLengthSamples = kickMs * 0.001 * r.sampleRate;

    // Vorlauf: mindestens 1¼ Viertel (längste Note) bzw. Kicklänge, auf Sechzehntel gerundet
    const double kickQn = kickMs * 0.001 * r.bpm / 60.0;
    const double preQn = std::ceil (std::max (1.25, kickQn + 0.25) * 4.0) / 4.0;
    const double startPpq = 4.0 - std::min (4.0, preQn);
    const double barStartPpq = 4.0;

    const int barLen = (int) std::lround (4.0 * spq);
    const int preLen = (int) std::lround ((barStartPpq - startPpq) * spq);
    const int total = preLen + barLen;

    constexpr int block = 256;
    SamsiEngine engine;
    engine.prepare (r.sampleRate, block);

    TriggerLog log;
    log.kickOn.reserve (64); log.bassOn.reserve (128); log.bassOff.reserve (128);
    engine.setTriggerLog (&log);

    r.kick.resize ((size_t) barLen);
    r.bass.resize ((size_t) barLen);
    r.mix.resize ((size_t) barLen);
    r.mixL.resize ((size_t) barLen);
    r.mixR.resize ((size_t) barLen);

    TransportInfo t;
    t.playing = true;
    t.hasPpq = true;
    t.bpm = r.bpm;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        t.ppq = startPpq + (double) pos / spq;
        engine.process (s, pattern, t, nullptr, 0, n, layers);

        for (int i = 0; i < n; ++i)
        {
            const int dst = pos + i - preLen;
            if (dst < 0 || dst >= barLen) continue;
            const float bl = engine.bassL()[i], br = engine.bassR()[i];
            r.kick[(size_t) dst] = engine.kick()[i];
            r.bass[(size_t) dst] = 0.5f * (bl + br);
            r.mixL[(size_t) dst] = engine.mixL()[i];
            r.mixR[(size_t) dst] = engine.mixR()[i];
            r.mix[(size_t) dst]  = 0.5f * (engine.mixL()[i] + engine.mixR()[i]);
        }
    }

    auto rel = [&] (const std::vector<int64_t>& src, std::vector<int>& dst)
    {
        for (auto v : src)
        {
            const int64_t p = v - preLen;
            if (p > -barLen && p < barLen) dst.push_back ((int) p);
        }
    };
    rel (log.kickOn, r.kickOn);
    rel (log.bassOn, r.bassOn);
    rel (log.bassOff, r.bassOff);
    return r;
}

} // namespace samsi
