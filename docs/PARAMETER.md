# Alle Parameter von Samsi Bassline

149 Parameter. Die IDs sind stabil (in `Source/Parameters.h` festgelegt) und dürfen nie umbenannt werden – Projekte und
Automationen in der DAW beziehen sich darauf. Neue Parameter werden nur hinzugefügt.
Nicht automatisierbar (ändern die Latenz bzw. nur die Anzeige): `midi_lookahead`, `u_ts_on`, `u_lim_on`, `u_post`.

## Kick (Synthese) (16)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `kick_level` | Kick Level | -48.0 dB ... 6.0 dB | -4.0 dB |
| `kick_start_hz` | Kick Start Freq | 40 Hz ... 3000 Hz | 480 Hz |
| `kick_end_hz` | Kick Fundamental | 25.0 Hz (G-1) ... 120.0 Hz (B1) | 46.2 Hz (F#0) |
| `kick_pitch_ms` | Kick Pitch Time | 2.0 ms ... 500.0 ms | 45.0 ms |
| `kick_pitch_curve` | Kick Pitch Curve | 0.30 ... 8.00 | 2.50 |
| `kick_attack_ms` | Kick Attack | 0.00 ms ... 10.00 ms | 0.30 ms |
| `kick_hold_ms` | Kick Hold | 0.0 ms ... 300.0 ms | 25.0 ms |
| `kick_decay_ms` | Kick Decay | 10 ms ... 1500 ms | 220 ms |
| `kick_decay_curve` | Kick Decay Curve | 0.30 ... 8.00 | 1.60 |
| `kick_tail_level` | Kick Tail Level | 0 % ... 100 % | 15 % |
| `kick_tail_ms` | Kick Tail Time | 0 ms ... 1500 ms | 120 ms |
| `kick_start_phase` | Kick Start Phase | 0.0 deg ... 360.0 deg | 0.0 deg |
| `kick_click_level` | Kick Click Level | -60.0 dB ... 0.0 dB | -16.0 dB |
| `kick_click_decay` | Kick Click Decay | 0.5 ms ... 30.0 ms | 4.0 ms |
| `kick_click_tone` | Kick Click Tone | 500 Hz ... 12000 Hz | 3500 Hz |
| `kick_drive` | Kick Drive | 0.0 dB ... 24.0 dB | 4.0 dB |

## Kick-Sample-Layer und Modulation (13)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `kick_smp_on` | Kick Sample On | aus / an | Off |
| `kick_smp_level` | Kick Sample Level | -48.0 dB ... 6.0 dB | -6.0 dB |
| `kick_smp_tune` | Kick Sample Tune | -24.00 st ... 24.00 st | 0.00 st |
| `kick_smp_offset` | Kick Sample Delay | 0.00 ms ... 30.00 ms | 0.00 ms |
| `kick_smp_start` | Kick Sample Start | 0.0 ms ... 200.0 ms | 0.0 ms |
| `kick_smp_length` | Kick Sample Length | 5 ms ... 2000 ms | 400 ms |
| `kick_smp_fade` | Kick Sample Fade | 1 ms ... 1000 ms | 60 ms |
| `kick_smp_lowcut` | Kick Sample Low Cut | 20 Hz ... 2000 Hz | 20 Hz |
| `kick_smp_invert` | Kick Sample Invert | aus / an | Off |
| `kick_smp_amp_env` | Kick Sample Amp Env | 0 % ... 100 % | 0 % |
| `kick_smp_pitch_env` | Kick Sample Pitch Env | 0 % ... 100 % | 0 % |
| `kick_smp_key` | Kick Sample Key Track | aus / an | Off |
| `kick_smp_drive` | Kick Sample Thru Drive | aus / an | Off |

## Bass (Synthese, Filter, Hüllkurven, Wavetable, Pick) (35)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `bass_level` | Bass Level | -48.0 dB ... 6.0 dB | -8.0 dB |
| `bass_saw` | Bass Saw | 0 % ... 100 % | 80 % |
| `bass_square` | Bass Square | 0 % ... 100 % | 0 % |
| `bass_pw` | Bass Pulse Width | 0.05 ... 0.95 | 0.50 |
| `bass_sub` | Bass Sub | 0 % ... 100 % | 50 % |
| `bass_sub_oct` | Bass Sub Octave | 0 / -1 | 0 |
| `bass_start_phase` | Bass Start Phase | 0.0 deg ... 360.0 deg | 0.0 deg |
| `bass_phase_reset` | Bass Phase Reset | aus / an | On |
| `bass_cutoff` | Bass Cutoff | 30 Hz ... 16000 Hz | 900 Hz |
| `bass_reso` | Bass Resonance | 0 % ... 100 % | 25 % |
| `bass_filter_mode` | Bass Filter Mode | LP12 / LP24 | LP24 |
| `bass_fenv_amt` | Bass Filter Env | -6.00 oct ... 6.00 oct | 2.50 oct |
| `bass_keytrack` | Bass Keytrack | 0 % ... 100 % | 50 % |
| `bass_fenv_att` | Bass F.Env Attack | 0.10 ms ... 50.00 ms | 0.30 ms |
| `bass_fenv_dec` | Bass F.Env Decay | 5 ms ... 2000 ms | 90 ms |
| `bass_fenv_sus` | Bass F.Env Sustain | 0 % ... 100 % | 0 % |
| `bass_fenv_rel` | Bass F.Env Release | 1 ms ... 500 ms | 20 ms |
| `bass_att` | Bass Attack | 0.10 ms ... 50.00 ms | 0.50 ms |
| `bass_dec` | Bass Decay | 5 ms ... 2000 ms | 140 ms |
| `bass_sus` | Bass Sustain | 0 % ... 100 % | 60 % |
| `bass_rel` | Bass Release | 1.0 ms ... 500.0 ms | 8.0 ms |
| `bass_vel_amp` | Bass Vel > Amp | 0 % ... 100 % | 30 % |
| `bass_vel_filt` | Bass Vel > Filter | 0 % ... 100 % | 40 % |
| `bass_drive` | Bass Drive | 0.0 dB ... 24.0 dB | 3.0 dB |
| `bass_glide` | Bass Glide | 0 ms ... 500 ms | 40 ms |
| `bass_width` | Bass Width | 0 % ... 100 % | 0 % |
| `bass_width_hz` | Bass Width Freq | 100 Hz ... 2000 Hz | 250 Hz |
| `bass_groove_ms` | Bass Groove Delay | -10.00 ms ... 10.00 ms | 0.00 ms |
| `bass_wt_level` | Bass Wavetable Level | 0 % ... 100 % | 0 % |
| `bass_wt_table` | Bass Wavetable | Klassik (Saw > Square > Pulse) / Holz (Formant) / Flüstern (Luft) / Sync / FM-Growl / Fold / Eigene Datei | Klassik (Saw > Square > Pulse) |
| `bass_wt_pos` | Bass Wavetable Position | 0 % ... 100 % | 0 % |
| `bass_wt_env` | Bass Wavetable Env | -1.00 ... 1.00 | 0.00 |
| `bass_pick_amt` | Bass Pick Amount | 0 % ... 100 % | 0 % |
| `bass_pick_pos` | Bass Pick Position | 0.05 ... 0.50 | 0.20 |
| `bass_damper` | Bass Damper | 0 % ... 100 % | 0 % |

## Bass-Sample und Modulation (5)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `bass_smp_level` | Bass Sample Level | 0 % ... 100 % | 0 % |
| `bass_smp_tune` | Bass Sample Tune | -24.00 st ... 24.00 st | 0.00 st |
| `bass_smp_start` | Bass Sample Start | 0.0 ms ... 300.0 ms | 0.0 ms |
| `bass_smp_thru` | Bass Sample Thru Filter | aus / an | On |
| `bass_smp_pitch_env` | Bass Sample Pitch Env | -24.00 st ... 24.00 st | 0.00 st |

## Auto Fit (Ergebnisse und Sperren) (16)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `fit_enable` | Fit Active (A/B) | aus / an | On |
| `fit_phase` | Fit Bass Phase | 0.0 deg ... 360.0 deg | 0.0 deg |
| `fit_polarity` | Fit Bass Polarity Invert | aus / an | Off |
| `fit_offset_ms` | Fit Bass Offset | -10.00 ms ... 10.00 ms | 0.00 ms |
| `fit_duck_db` | Fit Duck Depth | 0.0 dB ... 18.0 dB | 0.0 dB |
| `fit_duck_ms` | Fit Duck Time | 5 ms ... 400 ms | 80 ms |
| `fit_duck_low_only` | Fit Duck Low Band Only | aus / an | On |
| `fit_duck_hz` | Fit Duck Freq | 60 Hz ... 400 Hz | 150 Hz |
| `fit_trim_db` | Fit Bass Trim | -3.00 dB ... 3.00 dB | 0.00 dB |
| `fit_bass_att_add` | Fit Bass Attack Add | 0.00 ms ... 10.00 ms | 0.00 ms |
| `fit_kick_tail` | Fit Kick Tail | 50 % ... 100 % | 100 % |
| `fit_phase_mode` | Fit Phase Mode | Fixed / Follow Kick | Fixed |
| `lock_kick` | Lock Kick | aus / an | On |
| `lock_timing` | Lock Timing | aus / an | On |
| `lock_bass_tone` | Lock Bass Tone | aus / an | Off |
| `fit_max_shift_ms` | Fit Max Time Shift | 0.00 ms ... 5.00 ms | 1.50 ms |

## Kick→Bass-Glue (Sidechain-Kompressor, dynamischer EQ) (28)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `glue_on` | Glue On | aus / an | Off |
| `glue_comp_on` | Glue Comp On | aus / an | On |
| `glue_comp_style` | Glue Comp Style | Clean / Pumping | Clean |
| `glue_comp_thr` | Glue Comp Threshold | -60.0 dB ... 0.0 dB | -24.0 dB |
| `glue_comp_ratio` | Glue Comp Ratio | 1.00 :1 ... 20.00 :1 | 3.00 :1 |
| `glue_comp_att` | Glue Comp Attack | 0.05 ms ... 50.00 ms | 1.00 ms |
| `glue_comp_rel` | Glue Comp Release | 5 ms ... 1000 ms | 80 ms |
| `glue_comp_knee` | Glue Comp Knee | 0.0 dB ... 24.0 dB | 6.0 dB |
| `glue_comp_range` | Glue Comp Range | 0.0 dB ... 30.0 dB | 6.0 dB |
| `glue_comp_sc_hp` | Glue Comp SC Highpass | 20 Hz ... 500 Hz | 20 Hz |
| `glue_comp_sc_lp` | Glue Comp SC Lowpass | 50 Hz ... 5000 Hz | 300 Hz |
| `glue_comp_mix` | Glue Comp Mix | 0 % ... 100 % | 100 % |
| `glue_b1_on` | Glue Band 1 On | aus / an | Off |
| `glue_b1_type` | Glue Band 1 Type | Bell / Low Shelf | Bell |
| `glue_b1_freq` | Glue Band 1 Freq | 20.0 Hz ... 2000.0 Hz | 55.0 Hz |
| `glue_b1_q` | Glue Band 1 Q | 0.20 ... 10.00 | 1.00 |
| `glue_b1_range` | Glue Band 1 Range | -24.0 dB ... 24.0 dB | -6.0 dB |
| `glue_b1_thr` | Glue Band 1 Threshold | -60.0 dB ... 0.0 dB | -30.0 dB |
| `glue_b1_att` | Glue Band 1 Attack | 0.05 ms ... 50.00 ms | 1.00 ms |
| `glue_b1_rel` | Glue Band 1 Release | 5 ms ... 1000 ms | 60 ms |
| `glue_b2_on` | Glue Band 2 On | aus / an | Off |
| `glue_b2_type` | Glue Band 2 Type | Bell / Low Shelf | Bell |
| `glue_b2_freq` | Glue Band 2 Freq | 20.0 Hz ... 2000.0 Hz | 110.0 Hz |
| `glue_b2_q` | Glue Band 2 Q | 0.20 ... 10.00 | 1.00 |
| `glue_b2_range` | Glue Band 2 Range | -24.0 dB ... 24.0 dB | -3.0 dB |
| `glue_b2_thr` | Glue Band 2 Threshold | -60.0 dB ... 0.0 dB | -30.0 dB |
| `glue_b2_att` | Glue Band 2 Attack | 0.05 ms ... 50.00 ms | 1.00 ms |
| `glue_b2_rel` | Glue Band 2 Release | 5 ms ... 1000 ms | 60 ms |

## Sequencer, MIDI, Tonart, Master, Mix-Bus (15)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `seq_on` | Sequencer On | aus / an | On |
| `seq_swing` | Swing | 0 % ... 60 % | 0 % |
| `seq_root` | Root Note | C-1 ... C3 | F#0 |
| `midi_kick_note` | MIDI Kick Note | C-2 (0) ... G8 (127) | C1 (36) |
| `master_db` | Master | -24.0 dB ... 6.0 dB | 0.0 dB |
| `bus_clip_on` | Mix Bus Clipper (nonlinear) | aus / an | Off |
| `bus_clip_drive` | Mix Bus Clip Drive | 0.0 dB ... 12.0 dB | 0.0 dB |
| `ab_loud_match` | A/B Loudness Match | aus / an | On |
| `mix_monitor` | Mix Out Monitor (Solo) | Summe / Solo Kick / Solo Bass | Summe |
| `seq_mode` | Sequencer Mode | Transport / MIDI-Gate / MIDI-Gate + Transpose | Transport |
| `seq_grid` | Sequencer Grid | 1/16 / 1/16 Triole / 1/32 | 1/16 |
| `midi_lookahead` | MIDI Lookahead | Aus / 5 ms / 10 ms / 20 ms | Aus |
| `key_root` | Key | C / C# / D / D# / E / F / F# / G / G# / A / A# / H | F# |
| `key_scale` | Scale | Moll (Äolisch) / Phrygisch / Harmonisch Moll / Phrygisch-Dominant / Dur / Dorisch / Chromatisch (aus) | Phrygisch |
| `key_tolerance` | Key Tolerance | 2 ct ... 50 ct | 15 ct |

## Ausgangs-Utility und Limiter (21)

| ID | Name | Bereich | Standard |
|---|---|---|---|
| `u_on` | Utility On | aus / an | On |
| `u_post` | Meter Post Utility | aus / an | On |
| `u_inv_l` | Utility Invert L | aus / an | Off |
| `u_inv_r` | Utility Invert R | aus / an | Off |
| `u_swap` | Utility Swap L/R | aus / an | Off |
| `u_listen` | Utility Listen | ST / L / R / M / S / L Dual / R Dual | ST |
| `u_dc` | Utility DC Block | aus / an | Off |
| `u_mono_on` | Utility Mono Filter | aus / an | Off |
| `u_mono_hz` | Utility Mono Freq | 20 Hz ... 1000 Hz | 120 Hz |
| `u_imager` | Utility Imager | 0 % ... 400 % | 100 % |
| `u_tilt_ms` | Utility L/R Tilt | -20.00 ms ... 20.00 ms | 0.00 ms |
| `u_phase_on` | Utility Phase On | aus / an | Off |
| `u_phase_deg` | Utility Phase Rotation | 0.0 deg ... 360.0 deg | 0.0 deg |
| `u_ts_on` | Utility Time Shift On | aus / an | Off |
| `u_ts_ms` | Utility Time Shift | -50.00 ms ... 50.00 ms | 0.00 ms |
| `u_mix` | Utility Mix | 0 % ... 100 % | 100 % |
| `u_out_l` | Utility Output L | -60.0 dB ... 12.0 dB | 0.0 dB |
| `u_out_r` | Utility Output R | -60.0 dB ... 12.0 dB | 0.0 dB |
| `u_out_link` | Utility Output Link | aus / an | On |
| `u_lim_on` | Mix Limiter (nonlinear) | aus / an | Off |
| `u_lim_ceil` | Mix Limiter Ceiling | -24.0 dBFS ... 0.0 dBFS | -0.2 dBFS |

