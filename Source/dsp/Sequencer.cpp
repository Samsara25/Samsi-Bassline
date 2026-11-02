#include "Sequencer.h"

#include <algorithm>
#include <cmath>

namespace samsi
{

void EventList::sort() noexcept
{
    // Insertion-Sort: stabil, allokationsfrei, bei kleinen Listen schnell
    for (int i = 1; i < count; ++i)
    {
        const EngineEvent e = events[(size_t) i];
        int j = i - 1;
        while (j >= 0 && (events[(size_t) j].sample > e.sample
                          || (events[(size_t) j].sample == e.sample && events[(size_t) j].type > e.type)))
        {
            events[(size_t) j + 1] = events[(size_t) j];
            --j;
        }
        events[(size_t) j + 1] = e;
    }
}

double Sequencer::stepTimeQn (int64_t k, float swing, int grid) noexcept
{
    const double stepQn = stepQnForGrid (grid);
    double t = (double) k * stepQn;
    if ((k & 1) != 0 && grid != 1)
        t += (double) std::min (0.75f, std::max (0.0f, swing)) * 0.5 * stepQn;
    return t;
}

void Sequencer::collect (const PatternData& pattern, const GlobalSettings& g,
                         double anchorPpq, int64_t anchorSample, int64_t blockStart, double samplesPerQn,
                         int numSamples, double bassShiftQn, EventList& out) noexcept
{
    if (numSamples <= 0 || ! (samplesPerQn > 0.0) || ! std::isfinite (anchorPpq))
        return;

    const int64_t blockEnd = blockStart + numSamples;
    const double ppqStart = anchorPpq + (double) (blockStart - anchorSample) / samplesPerQn;
    const double ppqEnd   = anchorPpq + (double) (blockEnd - anchorSample) / samplesPerQn;

    // Rückblick: längste Note (4 Schritte) + Swing + Versatz
    const int grid = std::min (2, std::max (0, g.seqGrid));
    const double kStepQn = stepQnForGrid (grid);
    const int numSteps = stepsForGrid (grid);
    // Rückblick: längste Note (4 Schritte) + Swing + Versatz
    const int64_t kFirst = (int64_t) std::floor (ppqStart / kStepQn) - 8 - (int64_t) std::ceil (std::abs (bassShiftQn) / kStepQn);
    const int64_t kLast  = (int64_t) std::floor (ppqEnd / kStepQn) + 2 + (int64_t) std::ceil (std::abs (bassShiftQn) / kStepQn);

    // erstes Sample >= t (ganzzahlig -> blockgrößenunabhängig)
    auto sampleOf = [&] (double t) noexcept
    {
        return anchorSample + (int64_t) std::ceil ((t - anchorPpq) * samplesPerQn - 1.0e-7);
    };
    auto local = [&] (int64_t s) noexcept { return (int) (s - blockStart); };
    auto inBlock = [&] (int64_t s) noexcept { return s >= blockStart && s < blockEnd && s >= anchorSample; };

    for (int64_t k = kFirst; k <= kLast; ++k)
    {
        const int idx = (int) (((k % numSteps) + numSteps) % numSteps);
        const Step st = pattern.get (idx);
        const double base = stepTimeQn (k, g.swing, grid);
        const int id = (int) (k & 0x3fffffff);

        if (st.kick)
        {
            const int64_t s = sampleOf (base);
            if (inBlock (s))
            {
                EngineEvent e;
                e.sample = local (s);
                e.type = EngineEvent::KickOn;
                e.velocity = (float) st.kickVel / 127.0f;
                e.source = 0;
                e.id = id;
                out.add (e);
            }
        }

        if (st.bass)
        {
            const double on  = base + bassShiftQn;
            const double off = on + (double) st.lenPct * 0.01 * kStepQn;
            const int64_t sOn = sampleOf (on), sOff = sampleOf (off);

            if (inBlock (sOn))
            {
                EngineEvent e;
                e.sample = local (sOn);
                e.type = EngineEvent::BassOn;
                e.note = std::min (127, std::max (0, g.rootNote + st.note));
                e.velocity = (float) st.bassVel / 127.0f;
                e.slide = st.slide;
                e.source = 0;
                e.id = id;
                out.add (e);
            }
            if (inBlock (sOff))
            {
                EngineEvent e;
                e.sample = local (sOff);
                e.type = EngineEvent::BassOff;
                e.source = 0;
                e.id = id;
                out.add (e);
            }
        }
    }
}

} // namespace samsi
