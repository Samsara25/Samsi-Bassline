#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace samsi
{

/** Ein Abtastwert für die Live-Anzeigen (Scope, Spektrum, Goniometer, Meter, Live-Wellenformen). */
struct TapFrame
{
    float mixL = 0, mixR = 0;   // MIX-Ausgang (vor oder nach der Utility, je nach "Post")
    float kick = 0, bass = 0;   // KICK- und BASS-Ausgang (Mono-Summe /2)
    float scL = 0, scR = 0;     // Sidechain-Eingang (nur Anzeige)
    float beat = -1.0f;         // Position innerhalb des Viertels 0..1, -1 = keine Zeitleiste
    uint32_t flags = 0;         // Bit 0 Kick, Bit 1 Bassnote, Bit 2 Slide
};

/** Lock-freie Einzel-Produzent/Einzel-Konsument-Warteschlange (Audio-Thread schreibt,
    Oberfläche liest). Speicher wird nur im Konstruktor angelegt; ist sie voll, werden neue
    Werte verworfen (die Anzeige verliert dann Daten, der Audio-Thread blockiert nie). */
class LiveTap
{
public:
    explicit LiveTap (int capacity = 1 << 16) : fifo (capacity), data ((size_t) capacity) {}

    /** Audio-Thread */
    void push (const TapFrame* frames, int n) noexcept
    {
        if (! enabled.load (std::memory_order_relaxed)) return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) data[(size_t) (s1 + i)] = frames[i];
        for (int i = 0; i < n2; ++i) data[(size_t) (s2 + i)] = frames[n1 + i];
        fifo.finishedWrite (n1 + n2);
        if (n1 + n2 < n) dropped.fetch_add (n - n1 - n2, std::memory_order_relaxed);
    }

    /** Oberflächen-Thread: liest bis zu maxN Werte */
    int pull (TapFrame* dest, int maxN) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) dest[i] = data[(size_t) (s1 + i)];
        for (int i = 0; i < n2; ++i) dest[n1 + i] = data[(size_t) (s2 + i)];
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    /** Nur befüllen, solange eine Anzeige offen ist (spart CPU ohne Editor). */
    void setEnabled (bool e) noexcept { enabled.store (e); }
    bool isEnabled() const noexcept { return enabled.load(); }
    int64_t droppedFrames() const noexcept { return dropped.load(); }

private:
    juce::AbstractFifo fifo;
    std::vector<TapFrame> data;
    std::atomic<bool> enabled { false };
    std::atomic<int64_t> dropped { 0 };
};

} // namespace samsi
