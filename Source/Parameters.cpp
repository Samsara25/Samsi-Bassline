#include "Parameters.h"

#include "content/KeyAnalysis.h"
#include "content/WavetableFactory.h"

namespace samsi
{

using APF  = juce::AudioParameterFloat;
using APB  = juce::AudioParameterBool;
using APC  = juce::AudioParameterChoice;
using API  = juce::AudioParameterInt;
using Attr = juce::AudioParameterFloatAttributes;

namespace
{
    constexpr int kVersion = 1;   // Parameter-Version (VST3: stabil halten)

    juce::String fmt (float v, int decimals, const char* unit)
    {
        // Hinweis: juce::String (float, 0) gibt alle Stellen aus -> ganze Zahlen selbst runden
        const juce::String num = decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        return num + (unit[0] != 0 ? juce::String (" ") + unit : juce::String());
    }

    juce::String noteName (float hz)
    {
        if (hz <= 0.0f) return {};
        const int n = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
        return juce::MidiMessage::getMidiNoteName (n, true, true, 3);   // Ableton-Oktavierung (C3 = 60)
    }

    juce::NormalisableRange<float> logRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<APF> flt (const char* id, const char* name, juce::NormalisableRange<float> range,
                              float def, const char* unit, int decimals = 1)
    {
        auto u = juce::String (unit);
        return std::make_unique<APF> (juce::ParameterID { id, kVersion }, name, range, def,
            Attr().withLabel (u)
                  .withStringFromValueFunction ([decimals, u] (float v, int) { return fmt (v, decimals, u.toRawUTF8()); }));
    }

    std::unique_ptr<APF> pct (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, kVersion }, name,
            juce::NormalisableRange<float> (0.0f, 1.0f), def,
            Attr().withLabel ("%")
                  .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
                  .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() * 0.01f; }));
    }

    std::unique_ptr<APB> boo (const char* id, const char* name, bool def)
    {
        return std::make_unique<APB> (juce::ParameterID { id, kVersion }, name, def);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    //--- KICK ---------------------------------------------------------------
    auto kick = std::make_unique<juce::AudioProcessorParameterGroup> ("kick", "Kick", "|");
    kick->addChild (flt (pid::kLevel, "Kick Level", { -48.0f, 6.0f, 0.1f }, -4.0f, "dB"));
    kick->addChild (flt (pid::kStart, "Kick Start Freq", logRange (40.0f, 3000.0f, 400.0f), 480.0f, "Hz", 0));
    kick->addChild (std::make_unique<APF> (juce::ParameterID { pid::kEnd, kVersion }, "Kick Fundamental",
        logRange (25.0f, 120.0f, 50.0f), 46.25f,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
            { return juce::String (v, 1) + " Hz (" + noteName (v) + ")"; })));
    kick->addChild (flt (pid::kPitchMs, "Kick Pitch Time", logRange (2.0f, 500.0f, 50.0f), 45.0f, "ms"));
    kick->addChild (flt (pid::kPitchCurve, "Kick Pitch Curve", logRange (0.3f, 8.0f, 2.0f), 2.5f, "", 2));
    kick->addChild (flt (pid::kAttack, "Kick Attack", logRange (0.0f, 10.0f, 1.0f), 0.3f, "ms", 2));
    kick->addChild (flt (pid::kHold, "Kick Hold", logRange (0.0f, 300.0f, 40.0f), 25.0f, "ms"));
    kick->addChild (flt (pid::kDecay, "Kick Decay", logRange (10.0f, 1500.0f, 250.0f), 220.0f, "ms", 0));
    kick->addChild (flt (pid::kDecayCurve, "Kick Decay Curve", logRange (0.3f, 8.0f, 2.0f), 1.6f, "", 2));
    kick->addChild (pct (pid::kTailLevel, "Kick Tail Level", 0.15f));
    kick->addChild (flt (pid::kTailMs, "Kick Tail Time", logRange (0.0f, 1500.0f, 200.0f), 120.0f, "ms", 0));
    kick->addChild (flt (pid::kPhase, "Kick Start Phase", { 0.0f, 360.0f, 0.1f }, 0.0f, "deg"));
    kick->addChild (flt (pid::kClickLevel, "Kick Click Level", { -60.0f, 0.0f, 0.1f }, -16.0f, "dB"));
    kick->addChild (flt (pid::kClickDecay, "Kick Click Decay", logRange (0.5f, 30.0f, 5.0f), 4.0f, "ms"));
    kick->addChild (flt (pid::kClickTone, "Kick Click Tone", logRange (500.0f, 12000.0f, 3000.0f), 3500.0f, "Hz", 0));
    kick->addChild (flt (pid::kDrive, "Kick Drive", { 0.0f, 24.0f, 0.1f }, 4.0f, "dB"));
    kick->addChild (boo (pid::ksOn, "Kick Sample On", false));
    kick->addChild (flt (pid::ksLevel, "Kick Sample Level", { -48.0f, 6.0f, 0.1f }, -6.0f, "dB"));
    kick->addChild (flt (pid::ksTune, "Kick Sample Tune", { -24.0f, 24.0f, 0.01f }, 0.0f, "st", 2));
    kick->addChild (flt (pid::ksOffset, "Kick Sample Delay", { 0.0f, 30.0f, 0.01f }, 0.0f, "ms", 2));
    kick->addChild (flt (pid::ksStart, "Kick Sample Start", logRange (0.0f, 200.0f, 20.0f), 0.0f, "ms", 1));
    kick->addChild (flt (pid::ksLength, "Kick Sample Length", logRange (5.0f, 2000.0f, 200.0f), 400.0f, "ms", 0));
    kick->addChild (flt (pid::ksFade, "Kick Sample Fade", logRange (1.0f, 1000.0f, 60.0f), 60.0f, "ms", 0));
    kick->addChild (flt (pid::ksLowCut, "Kick Sample Low Cut", logRange (20.0f, 2000.0f, 200.0f), 20.0f, "Hz", 0));
    kick->addChild (boo (pid::ksInvert, "Kick Sample Invert", false));
    kick->addChild (pct (pid::ksAmpEnv, "Kick Sample Amp Env", 0.0f));
    kick->addChild (pct (pid::ksPitchEnv, "Kick Sample Pitch Env", 0.0f));
    kick->addChild (boo (pid::ksKey, "Kick Sample Key Track", false));
    kick->addChild (boo (pid::ksDrive, "Kick Sample Thru Drive", false));
    layout.add (std::move (kick));

    //--- BASS ---------------------------------------------------------------
    auto bass = std::make_unique<juce::AudioProcessorParameterGroup> ("bass", "Bass", "|");
    bass->addChild (flt (pid::bLevel, "Bass Level", { -48.0f, 6.0f, 0.1f }, -8.0f, "dB"));
    bass->addChild (pct (pid::bSaw, "Bass Saw", 0.8f));
    bass->addChild (pct (pid::bSquare, "Bass Square", 0.0f));
    bass->addChild (flt (pid::bPw, "Bass Pulse Width", { 0.05f, 0.95f, 0.001f }, 0.5f, "", 2));
    bass->addChild (pct (pid::bSub, "Bass Sub", 0.5f));
    bass->addChild (std::make_unique<APC> (juce::ParameterID { pid::bSubOct, kVersion }, "Bass Sub Octave",
                                           juce::StringArray { "0", "-1" }, 0));
    bass->addChild (flt (pid::bPhase, "Bass Start Phase", { 0.0f, 360.0f, 0.1f }, 0.0f, "deg"));
    bass->addChild (boo (pid::bPhaseReset, "Bass Phase Reset", true));
    bass->addChild (flt (pid::bCutoff, "Bass Cutoff", logRange (30.0f, 16000.0f, 800.0f), 900.0f, "Hz", 0));
    bass->addChild (pct (pid::bReso, "Bass Resonance", 0.25f));
    bass->addChild (std::make_unique<APC> (juce::ParameterID { pid::bFilterMode, kVersion }, "Bass Filter Mode",
                                           juce::StringArray { "LP12", "LP24" }, 1));
    bass->addChild (flt (pid::bEnvAmt, "Bass Filter Env", { -6.0f, 6.0f, 0.01f }, 2.5f, "oct", 2));
    bass->addChild (pct (pid::bKeytrack, "Bass Keytrack", 0.5f));
    bass->addChild (flt (pid::bFAtt, "Bass F.Env Attack", logRange (0.1f, 50.0f, 5.0f), 0.3f, "ms", 2));
    bass->addChild (flt (pid::bFDec, "Bass F.Env Decay", logRange (5.0f, 2000.0f, 150.0f), 90.0f, "ms", 0));
    bass->addChild (pct (pid::bFSus, "Bass F.Env Sustain", 0.0f));
    bass->addChild (flt (pid::bFRel, "Bass F.Env Release", logRange (1.0f, 500.0f, 50.0f), 20.0f, "ms", 0));
    bass->addChild (flt (pid::bAtt, "Bass Attack", logRange (0.1f, 50.0f, 5.0f), 0.5f, "ms", 2));
    bass->addChild (flt (pid::bDec, "Bass Decay", logRange (5.0f, 2000.0f, 200.0f), 140.0f, "ms", 0));
    bass->addChild (pct (pid::bSus, "Bass Sustain", 0.6f));
    bass->addChild (flt (pid::bRel, "Bass Release", logRange (1.0f, 500.0f, 40.0f), 8.0f, "ms", 1));
    bass->addChild (pct (pid::bVelAmp, "Bass Vel > Amp", 0.3f));
    bass->addChild (pct (pid::bVelFilt, "Bass Vel > Filter", 0.4f));
    bass->addChild (flt (pid::bDrive, "Bass Drive", { 0.0f, 24.0f, 0.1f }, 3.0f, "dB"));
    bass->addChild (flt (pid::bGlide, "Bass Glide", logRange (0.0f, 500.0f, 60.0f), 40.0f, "ms", 0));
    bass->addChild (pct (pid::bWidth, "Bass Width", 0.0f));
    bass->addChild (flt (pid::bWidthHz, "Bass Width Freq", logRange (100.0f, 2000.0f, 400.0f), 250.0f, "Hz", 0));
    bass->addChild (flt (pid::bGroove, "Bass Groove Delay", { -10.0f, 10.0f, 0.01f }, 0.0f, "ms", 2));
    {
        juce::StringArray tables;
        for (auto* n : kBuiltinTableNames) tables.add (juce::String::fromUTF8 (n));
        tables.add ("Eigene Datei");
        bass->addChild (pct (pid::wtLevel, "Bass Wavetable Level", 0.0f));
        bass->addChild (std::make_unique<APC> (juce::ParameterID { pid::wtTable, kVersion }, "Bass Wavetable", tables, 0));
        bass->addChild (pct (pid::wtPos, "Bass Wavetable Position", 0.0f));
        bass->addChild (flt (pid::wtEnv, "Bass Wavetable Env", { -1.0f, 1.0f, 0.001f }, 0.0f, "", 2));
        bass->addChild (pct (pid::pickAmt, "Bass Pick Amount", 0.0f));
        bass->addChild (flt (pid::pickPos, "Bass Pick Position", { 0.05f, 0.5f, 0.001f }, 0.2f, "", 2));
        bass->addChild (pct (pid::damper, "Bass Damper", 0.0f));
        bass->addChild (pct (pid::bsLevel, "Bass Sample Level", 0.0f));
        bass->addChild (flt (pid::bsTune, "Bass Sample Tune", { -24.0f, 24.0f, 0.01f }, 0.0f, "st", 2));
        bass->addChild (flt (pid::bsStart, "Bass Sample Start", logRange (0.0f, 300.0f, 30.0f), 0.0f, "ms", 1));
        bass->addChild (boo (pid::bsThru, "Bass Sample Thru Filter", true));
        bass->addChild (flt (pid::bsPitchEnv, "Bass Sample Pitch Env", { -24.0f, 24.0f, 0.01f }, 0.0f, "st", 2));
    }
    layout.add (std::move (bass));

    //--- AUTO FIT -----------------------------------------------------------
    auto fit = std::make_unique<juce::AudioProcessorParameterGroup> ("fit", "Auto Fit", "|");
    fit->addChild (boo (pid::fEnable, "Fit Active (A/B)", true));
    fit->addChild (flt (pid::fPhase, "Fit Bass Phase", { 0.0f, 360.0f, 0.1f }, 0.0f, "deg"));
    fit->addChild (boo (pid::fPolarity, "Fit Bass Polarity Invert", false));
    fit->addChild (flt (pid::fOffset, "Fit Bass Offset", { -10.0f, 10.0f, 0.01f }, 0.0f, "ms", 2));
    fit->addChild (flt (pid::fDuckDb, "Fit Duck Depth", { 0.0f, 18.0f, 0.1f }, 0.0f, "dB"));
    fit->addChild (flt (pid::fDuckMs, "Fit Duck Time", logRange (5.0f, 400.0f, 80.0f), 80.0f, "ms", 0));
    fit->addChild (boo (pid::fDuckLow, "Fit Duck Low Band Only", true));
    fit->addChild (flt (pid::fDuckHz, "Fit Duck Freq", logRange (60.0f, 400.0f, 150.0f), 150.0f, "Hz", 0));
    fit->addChild (flt (pid::fTrim, "Fit Bass Trim", { -3.0f, 3.0f, 0.01f }, 0.0f, "dB", 2));
    fit->addChild (flt (pid::fAttAdd, "Fit Bass Attack Add", { 0.0f, 10.0f, 0.01f }, 0.0f, "ms", 2));
    fit->addChild (flt (pid::fKickTail, "Fit Kick Tail", { 50.0f, 100.0f, 0.1f }, 100.0f, "%", 0));
    fit->addChild (std::make_unique<APC> (juce::ParameterID { pid::fPhaseMode, kVersion }, "Fit Phase Mode",
                                          juce::StringArray { "Fixed", "Follow Kick" }, 0));
    fit->addChild (boo (pid::lockKick, "Lock Kick", true));
    fit->addChild (boo (pid::lockTiming, "Lock Timing", true));
    fit->addChild (boo (pid::lockBassTone, "Lock Bass Tone", false));
    fit->addChild (flt (pid::maxShift, "Fit Max Time Shift", { 0.0f, 5.0f, 0.01f }, 1.5f, "ms", 2));
    layout.add (std::move (fit));

    //--- GLOBAL -------------------------------------------------------------
    auto glob = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    glob->addChild (boo (pid::seqOn, "Sequencer On", true));
    glob->addChild (std::make_unique<APF> (juce::ParameterID { pid::swing, kVersion }, "Swing",
        juce::NormalisableRange<float> (0.0f, 0.6f), 0.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));
    glob->addChild (std::make_unique<API> (juce::ParameterID { pid::root, kVersion }, "Root Note", 12, 60, 30,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
            { return juce::MidiMessage::getMidiNoteName (v, true, true, 3); })));
    glob->addChild (std::make_unique<API> (juce::ParameterID { pid::midiKick, kVersion }, "MIDI Kick Note", 0, 127, 36,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
            { return juce::MidiMessage::getMidiNoteName (v, true, true, 3) + " (" + juce::String (v) + ")"; })));
    glob->addChild (flt (pid::master, "Master", { -24.0f, 6.0f, 0.1f }, 0.0f, "dB"));
    glob->addChild (boo (pid::busClip, "Mix Bus Clipper (nonlinear)", false));
    glob->addChild (flt (pid::busClipDrive, "Mix Bus Clip Drive", { 0.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    glob->addChild (boo (pid::loudMatch, "A/B Loudness Match", true));
    glob->addChild (std::make_unique<APC> (juce::ParameterID { pid::monitor, kVersion }, "Mix Out Monitor (Solo)",
                                           juce::StringArray { "Summe", "Solo Kick", "Solo Bass" }, 0));
    glob->addChild (std::make_unique<APC> (juce::ParameterID { pid::seqMode, kVersion }, "Sequencer Mode",
                                           juce::StringArray { "Transport", "MIDI-Gate", "MIDI-Gate + Transpose" }, 0));
    glob->addChild (std::make_unique<APC> (juce::ParameterID { pid::seqGrid, kVersion }, "Sequencer Grid",
                                           juce::StringArray { "1/16", "1/16 Triole", "1/32" }, 0));
    // Lookahead ändert die Latenz -> nicht automatisierbar
    glob->addChild (std::make_unique<APC> (juce::ParameterID { pid::lookahead, kVersion }, "MIDI Lookahead",
                                           juce::StringArray { "Aus", "5 ms", "10 ms", "20 ms" }, 0,
                                           juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    {
        juce::StringArray keys, scales;
        for (auto* k : kKeyNames) keys.add (k);
        for (auto* sc : kScaleNames) scales.add (juce::String::fromUTF8 (sc));
        glob->addChild (std::make_unique<APC> (juce::ParameterID { pid::keyRoot, kVersion }, "Key", keys, 6));
        glob->addChild (std::make_unique<APC> (juce::ParameterID { pid::keyScale, kVersion }, "Scale", scales, 1));
        glob->addChild (flt (pid::keyTol, "Key Tolerance", { 2.0f, 50.0f, 1.0f }, 15.0f, "ct", 0));
    }
    layout.add (std::move (glob));

    //--- KICK -> BASS GLUE ------------------------------------------------------------
    auto gl = std::make_unique<juce::AudioProcessorParameterGroup> ("glue", "Glue", "|");
    gl->addChild (boo (pid::gOn, "Glue On", false));
    gl->addChild (boo (pid::gcOn, "Glue Comp On", true));
    gl->addChild (std::make_unique<APC> (juce::ParameterID { pid::gcStyle, kVersion }, "Glue Comp Style", juce::StringArray { "Clean", "Pumping" }, 0));
    gl->addChild (flt (pid::gcThr, "Glue Comp Threshold", { -60.0f, 0.0f, 0.1f }, -24.0f, "dB"));
    gl->addChild (flt (pid::gcRatio, "Glue Comp Ratio", logRange (1.0f, 20.0f, 4.0f), 3.0f, ":1", 2));
    gl->addChild (flt (pid::gcAtt, "Glue Comp Attack", logRange (0.05f, 50.0f, 3.0f), 1.0f, "ms", 2));
    gl->addChild (flt (pid::gcRel, "Glue Comp Release", logRange (5.0f, 1000.0f, 100.0f), 80.0f, "ms", 0));
    gl->addChild (flt (pid::gcKnee, "Glue Comp Knee", { 0.0f, 24.0f, 0.1f }, 6.0f, "dB"));
    gl->addChild (flt (pid::gcRange, "Glue Comp Range", { 0.0f, 30.0f, 0.1f }, 6.0f, "dB"));
    gl->addChild (flt (pid::gcScHp, "Glue Comp SC Highpass", logRange (20.0f, 500.0f, 60.0f), 20.0f, "Hz", 0));
    gl->addChild (flt (pid::gcScLp, "Glue Comp SC Lowpass", logRange (50.0f, 5000.0f, 300.0f), 300.0f, "Hz", 0));
    gl->addChild (pct (pid::gcMix, "Glue Comp Mix", 1.0f));
    struct BandIds { const char *on, *type, *freq, *q, *range, *thr, *att, *rel; float f, r; const char* name; };
    for (const auto& b : { BandIds { pid::d1On, pid::d1Type, pid::d1Freq, pid::d1Q, pid::d1Range, pid::d1Thr, pid::d1Att, pid::d1Rel, 55.0f, -6.0f, "Glue Band 1" },
                           BandIds { pid::d2On, pid::d2Type, pid::d2Freq, pid::d2Q, pid::d2Range, pid::d2Thr, pid::d2Att, pid::d2Rel, 110.0f, -3.0f, "Glue Band 2" } })
    {
        const juce::String n (b.name);
        gl->addChild (boo (b.on, (n + " On").toRawUTF8(), false));
        gl->addChild (std::make_unique<APC> (juce::ParameterID { b.type, kVersion }, n + " Type", juce::StringArray { "Bell", "Low Shelf" }, 0));
        gl->addChild (flt (b.freq, (n + " Freq").toRawUTF8(), logRange (20.0f, 2000.0f, 120.0f), b.f, "Hz", 1));
        gl->addChild (flt (b.q, (n + " Q").toRawUTF8(), logRange (0.2f, 10.0f, 1.0f), 1.0f, "", 2));
        gl->addChild (flt (b.range, (n + " Range").toRawUTF8(), { -24.0f, 24.0f, 0.1f }, b.r, "dB"));
        gl->addChild (flt (b.thr, (n + " Threshold").toRawUTF8(), { -60.0f, 0.0f, 0.1f }, -30.0f, "dB"));
        gl->addChild (flt (b.att, (n + " Attack").toRawUTF8(), logRange (0.05f, 50.0f, 3.0f), 1.0f, "ms", 2));
        gl->addChild (flt (b.rel, (n + " Release").toRawUTF8(), logRange (5.0f, 1000.0f, 100.0f), 60.0f, "ms", 0));
    }
    layout.add (std::move (gl));

    //--- AUSGANGS-UTILITY ---------------------------------------------------------
    auto ut = std::make_unique<juce::AudioProcessorParameterGroup> ("utility", "Utility", "|");
    ut->addChild (boo (pid::uOn, "Utility On", true));
    ut->addChild (std::make_unique<APB> (juce::ParameterID { pid::uPost, kVersion }, "Meter Post Utility", true,
                                         juce::AudioParameterBoolAttributes().withAutomatable (false)));
    ut->addChild (boo (pid::uInvL, "Utility Invert L", false));
    ut->addChild (boo (pid::uInvR, "Utility Invert R", false));
    ut->addChild (boo (pid::uSwap, "Utility Swap L/R", false));
    ut->addChild (std::make_unique<APC> (juce::ParameterID { pid::uListen, kVersion }, "Utility Listen",
                                         juce::StringArray { "ST", "L", "R", "M", "S", "L Dual", "R Dual" }, 0));
    ut->addChild (boo (pid::uDc, "Utility DC Block", false));
    ut->addChild (boo (pid::uMonoOn, "Utility Mono Filter", false));
    ut->addChild (flt (pid::uMonoHz, "Utility Mono Freq", logRange (20.0f, 1000.0f, 150.0f), 120.0f, "Hz", 0));
    ut->addChild (std::make_unique<APF> (juce::ParameterID { pid::uImager, kVersion }, "Utility Imager",
        juce::NormalisableRange<float> (0.0f, 4.0f, 0.0f, 0.5f), 1.0f,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() * 0.01f; })));
    ut->addChild (flt (pid::uTilt, "Utility L/R Tilt", { -20.0f, 20.0f, 0.001f, 0.6f, true }, 0.0f, "ms", 2));
    ut->addChild (boo (pid::uPhaseOn, "Utility Phase On", false));
    ut->addChild (flt (pid::uPhase, "Utility Phase Rotation", { 0.0f, 360.0f, 0.1f }, 0.0f, "deg"));
    // Time Shift ändert die Latenz -> Ein/Aus nicht automatisierbar
    ut->addChild (std::make_unique<APB> (juce::ParameterID { pid::uTsOn, kVersion }, "Utility Time Shift On", false,
                                         juce::AudioParameterBoolAttributes().withAutomatable (false)));
    ut->addChild (flt (pid::uTs, "Utility Time Shift", { -50.0f, 50.0f, 0.001f, 0.6f, true }, 0.0f, "ms", 2));
    ut->addChild (pct (pid::uMix, "Utility Mix", 1.0f));
    ut->addChild (flt (pid::uOutL, "Utility Output L", { -60.0f, 12.0f, 0.1f, 2.0f }, 0.0f, "dB"));
    ut->addChild (flt (pid::uOutR, "Utility Output R", { -60.0f, 12.0f, 0.1f, 2.0f }, 0.0f, "dB"));
    ut->addChild (boo (pid::uLink, "Utility Output Link", true));
    ut->addChild (std::make_unique<APB> (juce::ParameterID { pid::uLimOn, kVersion }, "Mix Limiter (nonlinear)", false,
                                         juce::AudioParameterBoolAttributes().withAutomatable (false)));
    ut->addChild (flt (pid::uLimCeil, "Mix Limiter Ceiling", { -24.0f, 0.0f, 0.1f }, -0.2f, "dBFS"));
    layout.add (std::move (ut));

    return layout;
}

//==============================================================================
ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& apvts)
{
   #define SAMSI_RESOLVE(name) name = apvts.getRawParameterValue (pid::name); jassert (name != nullptr);
    SAMSI_PARAM_LIST (SAMSI_RESOLVE)
   #undef SAMSI_RESOLVE
}

Settings ParamRefs::read() const noexcept
{
    auto f = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto b = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed) >= 0.5f; };
    auto i = [] (const std::atomic<float>* a) { return (int) std::lround (a->load (std::memory_order_relaxed)); };

    Settings s;
    auto& k = s.kick;
    k.levelDb = f (kLevel);   k.startHz = f (kStart);     k.endHz = f (kEnd);
    k.pitchMs = f (kPitchMs); k.pitchCurve = f (kPitchCurve);
    k.attackMs = f (kAttack); k.holdMs = f (kHold);       k.decayMs = f (kDecay);
    k.decayCurve = f (kDecayCurve); k.tailLevel = f (kTailLevel); k.tailMs = f (kTailMs);
    k.startPhaseDeg = f (kPhase);
    k.clickLevelDb = f (kClickLevel); k.clickDecayMs = f (kClickDecay); k.clickToneHz = f (kClickTone);
    k.driveDb = f (kDrive);

    auto& ba = s.bass;
    ba.levelDb = f (bLevel); ba.saw = f (bSaw); ba.square = f (bSquare); ba.pulseWidth = f (bPw);
    ba.sub = f (bSub); ba.subOctave = i (bSubOct) == 1 ? -1 : 0;
    ba.startPhaseDeg = f (bPhase); ba.phaseReset = b (bPhaseReset);
    ba.cutoffHz = f (bCutoff); ba.resonance = f (bReso);
    ba.filterMode = i (bFilterMode) == 0 ? FilterMode::LP12 : FilterMode::LP24;
    ba.envAmountOct = f (bEnvAmt); ba.keytrack = f (bKeytrack);
    ba.fAttackMs = f (bFAtt); ba.fDecayMs = f (bFDec); ba.fSustain = f (bFSus); ba.fReleaseMs = f (bFRel);
    ba.attackMs = f (bAtt); ba.decayMs = f (bDec); ba.sustain = f (bSus); ba.releaseMs = f (bRel);
    ba.velToAmp = f (bVelAmp); ba.velToFilter = f (bVelFilt); ba.driveDb = f (bDrive);
    ba.smpThru = b (bsThru); ba.smpPitchEnvSt = f (bsPitchEnv);
    ba.glideMs = f (bGlide); ba.width = f (bWidth); ba.widthHz = f (bWidthHz); ba.grooveDelayMs = f (bGroove);

    auto& ft = s.fit;
    ft.enabled = b (fEnable); ft.phaseDeg = f (fPhase); ft.polarity = b (fPolarity);
    ft.offsetMs = f (fOffset); ft.duckDb = f (fDuckDb); ft.duckMs = f (fDuckMs);
    ft.duckLowOnly = b (fDuckLow); ft.duckHz = f (fDuckHz); ft.trimDb = f (fTrim);
    ft.attackAddMs = f (fAttAdd); ft.kickTailPct = f (fKickTail);
    ft.phaseMode = i (fPhaseMode) == 1 ? PhaseMode::FollowKick : PhaseMode::Fixed;

    auto& c = s.constraints;
    c.lockKick = b (lockKick); c.lockTiming = b (lockTiming); c.lockBassTone = b (lockBassTone);
    c.maxShiftMs = f (maxShift);

    auto& g = s.global;
    g.seqOn = b (seqOn); g.swing = f (swing); g.rootNote = i (root); g.midiKickNote = i (midiKick);
    g.masterDb = f (master); g.busClipOn = b (busClip); g.busClipDriveDb = f (busClipDrive);
    g.loudMatch = b (loudMatch); g.monitor = juce::jlimit (0, 2, i (monitor));
    g.seqMode = juce::jlimit (0, 2, i (seqMode));
    g.seqGrid = juce::jlimit (0, 2, i (seqGrid));
    g.lookaheadMs = kLookaheadMs[juce::jlimit (0, 3, i (lookahead))];

    auto& gg = s.glue;
    gg.on = b (gOn); gg.compOn = b (gcOn); gg.style = i (gcStyle); gg.threshDb = f (gcThr); gg.ratio = f (gcRatio);
    gg.attackMs = f (gcAtt); gg.releaseMs = f (gcRel); gg.kneeDb = f (gcKnee); gg.rangeDb = f (gcRange);
    gg.scHpHz = f (gcScHp); gg.scLpHz = f (gcScLp); gg.mix = f (gcMix);
    {
        auto band = [&] (DynBandSettings& d, std::atomic<float>* on, std::atomic<float>* ty, std::atomic<float>* fr, std::atomic<float>* q,
                         std::atomic<float>* rg, std::atomic<float>* th, std::atomic<float>* at, std::atomic<float>* re)
        {
            d.on = b (on); d.type = i (ty); d.freqHz = f (fr); d.q = f (q); d.rangeDb = f (rg); d.threshDb = f (th); d.attackMs = f (at); d.releaseMs = f (re);
        };
        band (gg.band[0], d1On, d1Type, d1Freq, d1Q, d1Range, d1Thr, d1Att, d1Rel);
        band (gg.band[1], d2On, d2Type, d2Freq, d2Q, d2Range, d2Thr, d2Att, d2Rel);
    }

    auto& u = s.utility;
    u.on = b (uOn); u.post = b (uPost); u.invertL = b (uInvL); u.invertR = b (uInvR); u.swap = b (uSwap);
    u.listen = juce::jlimit (0, 6, i (uListen)); u.dcBlock = b (uDc); u.monoOn = b (uMonoOn); u.monoHz = f (uMonoHz);
    u.imager = f (uImager); u.tiltMs = f (uTilt); u.phaseOn = b (uPhaseOn); u.phaseDeg = f (uPhase);
    u.timeShiftOn = b (uTsOn); u.timeShiftMs = f (uTs); u.mix = f (uMix);
    u.outLDb = f (uOutL); u.outRDb = f (uOutR); u.outLink = b (uLink);
    u.limiterOn = b (uLimOn); u.limiterCeilDb = f (uLimCeil);
    g.keyRoot = juce::jlimit (0, 11, i (keyRoot));
    g.keyScale = juce::jlimit (0, kNumScales - 1, i (keyScale));
    g.keyTolCents = f (keyTol);

    k.smpOn = b (ksOn); k.smpLevelDb = f (ksLevel); k.smpTuneSt = f (ksTune); k.smpOffsetMs = f (ksOffset);
    k.smpStartMs = f (ksStart); k.smpLengthMs = f (ksLength); k.smpFadeMs = f (ksFade); k.smpLowCutHz = f (ksLowCut);
    k.smpInvert = b (ksInvert);
    k.smpAmpEnv = f (ksAmpEnv); k.smpPitchEnv = f (ksPitchEnv); k.smpKeyTrack = b (ksKey); k.smpDrive = b (ksDrive);
    ba.smpLevel = f (bsLevel); ba.smpTuneSt = f (bsTune); ba.smpStartMs = f (bsStart);

    ba.wtLevel = f (wtLevel); ba.wtTable = juce::jlimit (0, kNumTableSlots - 1, i (wtTable)); ba.wtPos = f (wtPos);
    ba.wtEnvAmt = f (wtEnv); ba.pickAmount = f (pickAmt); ba.pickPos = f (pickPos); ba.damper = f (damper);
    return s;
}

const std::vector<const char*>& fitResultParamIds()
{
    static const std::vector<const char*> ids { pid::fPhase, pid::fPolarity, pid::fOffset, pid::fDuckDb,
                                                pid::fDuckMs, pid::fTrim, pid::fAttAdd, pid::fKickTail,
                                                pid::fPhaseMode };
    return ids;
}

} // namespace samsi
