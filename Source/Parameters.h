#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Settings.h"

namespace samsi
{

/** Stabile Parameter-IDs. NIEMALS umbenennen – Ableton-Projekte und Automationen
    referenzieren diese IDs. Neue Parameter nur hinzufügen. */
namespace pid
{
    // Kick
    inline constexpr const char* kLevel = "kick_level", * kStart = "kick_start_hz", * kEnd = "kick_end_hz",
        * kPitchMs = "kick_pitch_ms", * kPitchCurve = "kick_pitch_curve", * kAttack = "kick_attack_ms",
        * kHold = "kick_hold_ms", * kDecay = "kick_decay_ms", * kDecayCurve = "kick_decay_curve",
        * kTailLevel = "kick_tail_level", * kTailMs = "kick_tail_ms", * kPhase = "kick_start_phase",
        * kClickLevel = "kick_click_level", * kClickDecay = "kick_click_decay", * kClickTone = "kick_click_tone",
        * kDrive = "kick_drive";
    // Kick-Sample-Layer
    inline constexpr const char* ksOn = "kick_smp_on", * ksLevel = "kick_smp_level", * ksTune = "kick_smp_tune",
        * ksOffset = "kick_smp_offset", * ksStart = "kick_smp_start", * ksLength = "kick_smp_length",
        * ksFade = "kick_smp_fade", * ksLowCut = "kick_smp_lowcut", * ksInvert = "kick_smp_invert";
    // Bass
    inline constexpr const char* bLevel = "bass_level", * bSaw = "bass_saw", * bSquare = "bass_square",
        * bPw = "bass_pw", * bSub = "bass_sub", * bSubOct = "bass_sub_oct", * bPhase = "bass_start_phase",
        * bPhaseReset = "bass_phase_reset", * bCutoff = "bass_cutoff", * bReso = "bass_reso",
        * bFilterMode = "bass_filter_mode", * bEnvAmt = "bass_fenv_amt", * bKeytrack = "bass_keytrack",
        * bFAtt = "bass_fenv_att", * bFDec = "bass_fenv_dec", * bFSus = "bass_fenv_sus", * bFRel = "bass_fenv_rel",
        * bAtt = "bass_att", * bDec = "bass_dec", * bSus = "bass_sus", * bRel = "bass_rel",
        * bVelAmp = "bass_vel_amp", * bVelFilt = "bass_vel_filt", * bDrive = "bass_drive", * bGlide = "bass_glide",
        * bWidth = "bass_width", * bWidthHz = "bass_width_hz", * bGroove = "bass_groove_ms";
    // Bass: Wavetable und Pick
    inline constexpr const char* wtLevel = "bass_wt_level", * wtTable = "bass_wt_table", * wtPos = "bass_wt_pos",
        * wtEnv = "bass_wt_env", * pickAmt = "bass_pick_amt", * pickPos = "bass_pick_pos", * damper = "bass_damper",
        * bsLevel = "bass_smp_level", * bsTune = "bass_smp_tune", * bsStart = "bass_smp_start";
    // Fit (Ergebnisse, manuell übersteuerbar)
    inline constexpr const char* fEnable = "fit_enable", * fPhase = "fit_phase", * fPolarity = "fit_polarity",
        * fOffset = "fit_offset_ms", * fDuckDb = "fit_duck_db", * fDuckMs = "fit_duck_ms",
        * fDuckLow = "fit_duck_low_only", * fDuckHz = "fit_duck_hz", * fTrim = "fit_trim_db",
        * fAttAdd = "fit_bass_att_add", * fKickTail = "fit_kick_tail", * fPhaseMode = "fit_phase_mode";
    // Fit-Vorgaben
    inline constexpr const char* lockKick = "lock_kick", * lockTiming = "lock_timing",
        * lockBassTone = "lock_bass_tone", * maxShift = "fit_max_shift_ms";
    // Global
    inline constexpr const char* seqOn = "seq_on", * swing = "seq_swing", * root = "seq_root",
        * midiKick = "midi_kick_note", * master = "master_db", * busClip = "bus_clip_on",
        * busClipDrive = "bus_clip_drive", * loudMatch = "ab_loud_match", * monitor = "mix_monitor",
        * seqMode = "seq_mode", * lookahead = "midi_lookahead", * seqGrid = "seq_grid",
        * keyRoot = "key_root", * keyScale = "key_scale", * keyTol = "key_tolerance";
    // Ausgangs-Utility und Messung
    inline constexpr const char* uOn = "u_on", * uPost = "u_post", * uInvL = "u_inv_l", * uInvR = "u_inv_r",
        * uSwap = "u_swap", * uListen = "u_listen", * uDc = "u_dc", * uMonoOn = "u_mono_on", * uMonoHz = "u_mono_hz",
        * uImager = "u_imager", * uTilt = "u_tilt_ms", * uPhaseOn = "u_phase_on", * uPhase = "u_phase_deg",
        * uTsOn = "u_ts_on", * uTs = "u_ts_ms", * uMix = "u_mix", * uOutL = "u_out_l", * uOutR = "u_out_r",
        * uLink = "u_out_link", * uLimOn = "u_lim_on", * uLimCeil = "u_lim_ceil";
    // Sample-Modulation durch die Kick- und Bass-Regler
    inline constexpr const char* ksAmpEnv = "kick_smp_amp_env", * ksPitchEnv = "kick_smp_pitch_env", * ksKey = "kick_smp_key",
        * ksDrive = "kick_smp_drive", * bsThru = "bass_smp_thru", * bsPitchEnv = "bass_smp_pitch_env";
    // Kick→Bass-Glue: Sidechain-Kompressor + dynamischer EQ auf dem Bass
    inline constexpr const char* gOn = "glue_on", * gcOn = "glue_comp_on", * gcStyle = "glue_comp_style",
        * gcThr = "glue_comp_thr", * gcRatio = "glue_comp_ratio", * gcAtt = "glue_comp_att", * gcRel = "glue_comp_rel",
        * gcKnee = "glue_comp_knee", * gcRange = "glue_comp_range", * gcScHp = "glue_comp_sc_hp", * gcScLp = "glue_comp_sc_lp",
        * gcMix = "glue_comp_mix",
        * d1On = "glue_b1_on", * d1Type = "glue_b1_type", * d1Freq = "glue_b1_freq", * d1Q = "glue_b1_q", * d1Range = "glue_b1_range",
        * d1Thr = "glue_b1_thr", * d1Att = "glue_b1_att", * d1Rel = "glue_b1_rel",
        * d2On = "glue_b2_on", * d2Type = "glue_b2_type", * d2Freq = "glue_b2_freq", * d2Q = "glue_b2_q", * d2Range = "glue_b2_range",
        * d2Thr = "glue_b2_thr", * d2Att = "glue_b2_att", * d2Rel = "glue_b2_rel";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

#define SAMSI_PARAM_LIST(X) \
    X(kLevel) X(kStart) X(kEnd) X(kPitchMs) X(kPitchCurve) X(kAttack) X(kHold) X(kDecay) X(kDecayCurve) \
    X(kTailLevel) X(kTailMs) X(kPhase) X(kClickLevel) X(kClickDecay) X(kClickTone) X(kDrive) \
    X(bLevel) X(bSaw) X(bSquare) X(bPw) X(bSub) X(bSubOct) X(bPhase) X(bPhaseReset) X(bCutoff) X(bReso) \
    X(bFilterMode) X(bEnvAmt) X(bKeytrack) X(bFAtt) X(bFDec) X(bFSus) X(bFRel) X(bAtt) X(bDec) X(bSus) X(bRel) \
    X(bVelAmp) X(bVelFilt) X(bDrive) X(bGlide) X(bWidth) X(bWidthHz) X(bGroove) \
    X(fEnable) X(fPhase) X(fPolarity) X(fOffset) X(fDuckDb) X(fDuckMs) X(fDuckLow) X(fDuckHz) X(fTrim) \
    X(fAttAdd) X(fKickTail) X(fPhaseMode) \
    X(lockKick) X(lockTiming) X(lockBassTone) X(maxShift) \
    X(seqOn) X(swing) X(root) X(midiKick) X(master) X(busClip) X(busClipDrive) X(loudMatch) X(monitor) \
    X(ksOn) X(ksLevel) X(ksTune) X(ksOffset) X(ksStart) X(ksLength) X(ksFade) X(ksLowCut) \
    X(wtLevel) X(wtTable) X(wtPos) X(wtEnv) X(pickAmt) X(pickPos) X(damper) \
    X(seqMode) X(lookahead) X(keyRoot) X(keyScale) X(keyTol) X(seqGrid) \
    X(ksInvert) X(bsLevel) X(bsTune) X(bsStart) \
    X(uOn) X(uPost) X(uInvL) X(uInvR) X(uSwap) X(uListen) X(uDc) X(uMonoOn) X(uMonoHz) X(uImager) X(uTilt) \
    X(uPhaseOn) X(uPhase) X(uTsOn) X(uTs) X(uMix) X(uOutL) X(uOutR) X(uLink) X(uLimOn) X(uLimCeil) \
    X(ksAmpEnv) X(ksPitchEnv) X(ksKey) X(ksDrive) X(bsThru) X(bsPitchEnv) \
    X(gOn) X(gcOn) X(gcStyle) X(gcThr) X(gcRatio) X(gcAtt) X(gcRel) X(gcKnee) X(gcRange) X(gcScHp) X(gcScLp) X(gcMix) \
    X(d1On) X(d1Type) X(d1Freq) X(d1Q) X(d1Range) X(d1Thr) X(d1Att) X(d1Rel) \
    X(d2On) X(d2Type) X(d2Freq) X(d2Q) X(d2Range) X(d2Thr) X(d2Att) X(d2Rel)

/** Direkte, lock-freie Zeiger auf die Parameterwerte (std::atomic<float>). */
class ParamRefs
{
public:
    explicit ParamRefs (juce::AudioProcessorValueTreeState& apvts);

    /** Liest alle Parameter in ein Settings-Objekt (Audio- und Message-Thread geeignet).
        abGainDb wird vom Aufrufer gesetzt (Analyseergebnis, kein Parameter). */
    Settings read() const noexcept;

private:
   #define SAMSI_DECL(name) std::atomic<float>* name = nullptr;
    SAMSI_PARAM_LIST (SAMSI_DECL)
   #undef SAMSI_DECL
};

/** Liste der Fit-Ergebnis-Parameter (für Undo und Übernahme). */
const std::vector<const char*>& fitResultParamIds();

/** Lookahead-Stufen in ms (Index = Parameterwert). */
inline constexpr int kLookaheadMs[4] = { 0, 5, 10, 20 };

} // namespace samsi
