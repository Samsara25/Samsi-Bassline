#include "Presets.h"

#include "../Parameters.h"

#include <algorithm>
#include <climits>

namespace samsi
{

namespace
{
    constexpr int X = INT_MIN;   // kein Bass auf diesem Schritt

    struct P
    {
        std::array<int, 16> notes;
        std::array<bool, 16> kick;
        std::array<bool, 16> slide {};
        int lenPct = 75;
        std::array<int, 16> vel {};
    };

    PatternData build (const P& p)
    {
        PatternData d = PatternData::makeEmpty();
        for (int i = 0; i < 16; ++i)    // Werkstatt-Presets sind 16-Schritt-Muster
        {
            Step s;
            s.kick = p.kick[(size_t) i];
            s.kickVel = 115;
            s.bass = p.notes[(size_t) i] != X;
            s.note = s.bass ? p.notes[(size_t) i] : 0;
            s.slide = p.slide[(size_t) i];
            s.lenPct = p.lenPct;
            s.bassVel = p.vel[(size_t) i] > 0 ? p.vel[(size_t) i] : 100;
            d.set (i, s);
        }
        return d;
    }

    constexpr std::array<bool, 16> fourOnFloor { 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0 };

    std::array<int, 16> rollVel()
    {
        return { 0,110,100,94, 0,110,100,94, 0,110,100,94, 0,110,100,94 };
    }

    //==========================================================================
    // 25 aus den Referenztracks abgeleitete und gemessene Presets (presets/*.py)
    struct GenPreset
    {
        const char* name;
        const char* theme;
        const char* hint;
        std::vector<std::pair<const char*, float>> values;
        std::vector<std::array<int, 7>> steps;
    };

    const std::vector<GenPreset>& generated()
    {
        static const std::vector<GenPreset> g {
           #include "PresetData.inc"
        };
        return g;
    }

    PatternData buildSteps (const std::vector<std::array<int, 7>>& st)
    {
        PatternData d = PatternData::makeEmpty();
        for (int i = 0; i < std::min (kNumSteps, (int) st.size()); ++i)
        {
            const auto& e = st[(size_t) i];
            Step s;
            s.kick = e[0] != 0; s.kickVel = e[1]; s.bass = e[2] != 0; s.bassVel = e[3];
            s.note = e[4]; s.lenPct = e[5]; s.slide = e[6] != 0;
            d.set (i, s);
        }
        return d;
    }

    std::vector<FactoryPreset> make()
    {
        std::vector<FactoryPreset> v;
        v.push_back ({ "Init · Samsi", "Init", "Grundeinstellung · F# Phrygisch · K-B-B-B", {}, PatternData::makeDefault() });

        for (const auto& g : generated())
            v.push_back ({ g.name, g.theme, g.hint, g.values, buildSteps (g.steps) });

        // Werkstatt-Presets aus v0.3 (langsamere Tempi, 16 Schritte) ----------------

        // 2 ----------------------------------------------------------------------
        {
            P p { { X,0,0,0, X,0,0,0, X,0,0,1, X,0,12,3 }, fourOnFloor };
            p.lenPct = 55; p.vel = rollVel();
            v.push_back ({ "Holzig Darkpsy", "Werkstatt",
                           "148-152 BPM · F Phrygisch · trockener Holz-Tock, Formant-Wavetable, rollend",
                           { { pid::kStart, 420.0f }, { pid::kEnd, 43.65f }, { pid::kPitchMs, 38.0f }, { pid::kPitchCurve, 3.0f },
                             { pid::kHold, 18.0f }, { pid::kDecay, 180.0f }, { pid::kDecayCurve, 2.0f }, { pid::kTailLevel, 0.10f },
                             { pid::kTailMs, 80.0f }, { pid::kClickLevel, -12.0f }, { pid::kClickDecay, 2.5f }, { pid::kClickTone, 1800.0f },
                             { pid::kDrive, 6.0f },
                             { pid::bSaw, 0.3f }, { pid::bSub, 0.6f }, { pid::wtLevel, 0.8f }, { pid::wtTable, 1.0f }, { pid::wtPos, 0.35f },
                             { pid::wtEnv, 0.4f }, { pid::bCutoff, 520.0f }, { pid::bReso, 0.45f }, { pid::bEnvAmt, 2.2f },
                             { pid::bKeytrack, 0.6f }, { pid::bFDec, 70.0f }, { pid::bAtt, 0.3f }, { pid::bDec, 95.0f }, { pid::bSus, 0.35f },
                             { pid::bRel, 6.0f }, { pid::bDrive, 6.0f }, { pid::bGlide, 30.0f }, { pid::bWidth, 0.15f }, { pid::bWidthHz, 400.0f },
                             { pid::root, 29.0f }, { pid::keyRoot, 5.0f }, { pid::keyScale, 1.0f } },
                           build (p) });
        }

        // 3 ----------------------------------------------------------------------
        {
            P p { { X,0,0,0, X,0,0,0, X,0,0,0, X,0,-2,0 }, fourOnFloor };
            p.lenPct = 70; p.vel = { 0,112,82,96, 0,112,82,96, 0,112,82,96, 0,112,86,96 };
            v.push_back ({ "Dark Flüstern", "Werkstatt",
                           "145-150 BPM · F# Moll · gehauchte Luft-Obertöne, leise Ghost-Noten, Breite nur oben",
                           { { pid::kStart, 380.0f }, { pid::kEnd, 46.25f }, { pid::kPitchMs, 50.0f }, { pid::kPitchCurve, 2.2f },
                             { pid::kHold, 22.0f }, { pid::kDecay, 240.0f }, { pid::kTailLevel, 0.18f }, { pid::kTailMs, 140.0f },
                             { pid::kClickLevel, -20.0f }, { pid::kClickTone, 6000.0f }, { pid::kClickDecay, 6.0f }, { pid::kDrive, 3.0f },
                             { pid::bSaw, 0.2f }, { pid::bSub, 0.7f }, { pid::wtLevel, 0.7f }, { pid::wtTable, 2.0f }, { pid::wtPos, 0.6f },
                             { pid::wtEnv, 0.3f }, { pid::bCutoff, 1400.0f }, { pid::bReso, 0.15f }, { pid::bEnvAmt, 1.2f },
                             { pid::bFDec, 140.0f }, { pid::bAtt, 1.5f }, { pid::bDec, 160.0f }, { pid::bSus, 0.5f }, { pid::bRel, 12.0f },
                             { pid::bDrive, 2.0f }, { pid::bWidth, 0.4f }, { pid::bWidthHz, 600.0f }, { pid::bVelAmp, 0.5f },
                             { pid::root, 30.0f }, { pid::keyRoot, 6.0f }, { pid::keyScale, 0.0f } },
                           build (p) });
        }

        // 4 ----------------------------------------------------------------------
        {
            P p { { X,0,12,0, X,0,1,0, X,0,12,0, X,3,1,0 }, fourOnFloor };
            p.lenPct = 45; p.vel = rollVel();
            v.push_back ({ "Alpen · Core", "Werkstatt",
                           "160-175 BPM · E Phrygisch · harter Kick, Fold-Wavetable, aggressiver Drive",
                           { { pid::kStart, 600.0f }, { pid::kEnd, 41.2f }, { pid::kPitchMs, 30.0f }, { pid::kPitchCurve, 3.5f },
                             { pid::kHold, 12.0f }, { pid::kDecay, 150.0f }, { pid::kTailLevel, 0.08f }, { pid::kTailMs, 60.0f },
                             { pid::kClickLevel, -10.0f }, { pid::kClickTone, 3000.0f }, { pid::kClickDecay, 2.0f }, { pid::kDrive, 10.0f },
                             { pid::bSaw, 0.5f }, { pid::bSquare, 0.3f }, { pid::bSub, 0.4f }, { pid::wtLevel, 0.6f }, { pid::wtTable, 5.0f },
                             { pid::wtPos, 0.5f }, { pid::wtEnv, 0.5f }, { pid::bCutoff, 800.0f }, { pid::bReso, 0.55f },
                             { pid::bEnvAmt, 3.0f }, { pid::bFDec, 55.0f }, { pid::bAtt, 0.2f }, { pid::bDec, 70.0f }, { pid::bSus, 0.2f },
                             { pid::bRel, 4.0f }, { pid::bDrive, 12.0f }, { pid::bGlide, 20.0f },
                             { pid::root, 28.0f }, { pid::keyRoot, 4.0f }, { pid::keyScale, 1.0f } },
                           build (p) });
        }

        // 5 ----------------------------------------------------------------------
        {
            P p { { X,0,0,7, X,0,3,0, X,0,0,5, X,10,0,0 }, fourOnFloor };
            p.slide = { 0,0,0,1, 0,0,0,0, 0,0,0,1, 0,0,1,0 };
            p.lenPct = 80; p.vel = rollVel();
            v.push_back ({ "Zone · Animiert", "Werkstatt",
                           "145-148 BPM · G Moll · Wavetable-Bewegung per Hüllkurve, Slides, leichter Swing",
                           { { pid::kStart, 450.0f }, { pid::kEnd, 49.0f }, { pid::kPitchMs, 42.0f }, { pid::kHold, 20.0f },
                             { pid::kDecay, 200.0f }, { pid::kTailLevel, 0.12f }, { pid::kTailMs, 100.0f }, { pid::kClickLevel, -14.0f },
                             { pid::kClickTone, 4000.0f }, { pid::kDrive, 5.0f },
                             { pid::bSaw, 0.4f }, { pid::bSub, 0.5f }, { pid::wtLevel, 0.9f }, { pid::wtTable, 4.0f }, { pid::wtPos, 0.2f },
                             { pid::wtEnv, 0.8f }, { pid::bCutoff, 700.0f }, { pid::bReso, 0.4f }, { pid::bEnvAmt, 2.8f },
                             { pid::bFDec, 110.0f }, { pid::bAtt, 0.5f }, { pid::bDec, 130.0f }, { pid::bSus, 0.45f }, { pid::bGlide, 60.0f },
                             { pid::bDrive, 5.0f }, { pid::bWidth, 0.3f }, { pid::swing, 0.08f },
                             { pid::root, 31.0f }, { pid::keyRoot, 7.0f }, { pid::keyScale, 0.0f } },
                           build (p) });
        }

        // 6 ----------------------------------------------------------------------
        {
            P p { { X,X,0,0, X,X,0,0, X,X,0,0, X,X,0,1 }, fourOnFloor };
            p.lenPct = 60; p.vel = { 0,0,110,96, 0,0,110,96, 0,0,110,96, 0,0,110,100 };
            v.push_back ({ "Offbeat Wald", "Werkstatt",
                           "138-145 BPM · A Phrygisch · Offbeat-Bass mit Sync-Wavetable, waldig-dunkel",
                           { { pid::kStart, 400.0f }, { pid::kEnd, 55.0f }, { pid::kPitchMs, 48.0f }, { pid::kHold, 30.0f },
                             { pid::kDecay, 260.0f }, { pid::kTailLevel, 0.2f }, { pid::kTailMs, 150.0f }, { pid::kClickLevel, -18.0f },
                             { pid::kClickTone, 2500.0f }, { pid::kDrive, 4.0f },
                             { pid::bSaw, 0.3f }, { pid::bSub, 0.6f }, { pid::wtLevel, 0.7f }, { pid::wtTable, 3.0f }, { pid::wtPos, 0.3f },
                             { pid::wtEnv, 0.5f }, { pid::bCutoff, 650.0f }, { pid::bReso, 0.35f }, { pid::bEnvAmt, 2.0f },
                             { pid::bFDec, 90.0f }, { pid::bDec, 150.0f }, { pid::bSus, 0.4f }, { pid::bDrive, 4.0f },
                             { pid::root, 33.0f }, { pid::keyRoot, 9.0f }, { pid::keyScale, 1.0f } },
                           build (p) });
        }

        // 7 ----------------------------------------------------------------------
        {
            P p { { X,0,0,0, X,0,0,0, X,0,0,0, X,0,7,0 }, fourOnFloor };
            p.lenPct = 65; p.vel = rollVel();
            v.push_back ({ "Zupf-Bass (Pick)", "Werkstatt",
                           "142-148 BPM · F# Moll · gezupfter Bass: Anschlagposition + Dämpfer, knackig und trocken",
                           { { pid::kEnd, 46.25f }, { pid::kDecay, 210.0f },
                             { pid::bSaw, 1.0f }, { pid::bSub, 0.4f }, { pid::pickAmt, 0.75f }, { pid::pickPos, 0.12f },
                             { pid::damper, 0.5f }, { pid::bCutoff, 1800.0f }, { pid::bReso, 0.1f }, { pid::bEnvAmt, 1.0f },
                             { pid::bFDec, 60.0f }, { pid::bAtt, 0.2f }, { pid::bDec, 120.0f }, { pid::bSus, 0.3f }, { pid::bRel, 10.0f },
                             { pid::bDrive, 1.5f },
                             { pid::root, 30.0f }, { pid::keyRoot, 6.0f }, { pid::keyScale, 0.0f } },
                           build (p) });
        }

        return v;
    }
}

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = make();
    return presets;
}

} // namespace samsi
