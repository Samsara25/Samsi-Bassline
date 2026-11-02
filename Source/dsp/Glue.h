#pragma once

#include "DspUtils.h"
#include "Settings.h"

namespace samsi
{

/** Kick→Bass-Glue: Sidechain-Kompressor + zwei dynamische EQ-Bänder auf dem Bass,
    Schlüssel ist das Kick-Signal derselben Instanz (gleiches Sample -> keine Latenz nötig).

    Kompressor (Feed-Forward): Schlüssel durch Sidechain-Hochpass/-Tiefpass, Spitzen-Detektor,
    statische Kennlinie mit weichem Knie (Threshold, Ratio, Knee), Begrenzung auf "Range",
    Attack/Release-Glättung der Pegelreduktion (Clean: in dB; Pumping: linear, längere Rückkehr),
    parallele Mischung (Mix).
    Dynamischer EQ: je Band ein Glockenfilter oder Low-Shelf (TPT-SVF, Koeffizienten pro Sample),
    dessen Verstärkung vom Kick-Anteil IM SELBEN Band gesteuert wird (Bandpass-Schlüssel,
    Ratio 4:1 oberhalb des Thresholds, begrenzt auf Range, Attack/Release). Ohne Kick: neutral. */
class KickBassGlue
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    /** key: Kick-Abtastwert; bl/br: Bass L/R (werden verändert) */
    void process (float key, float& bl, float& br, const GlueSettings& g) noexcept;
    /** Einmal je Block aufrufen: Filter-/Zeitkonstanten aus den Einstellungen */
    void update (const GlueSettings& g) noexcept;

    // Anzeige: aktuelle Werte (vom Audio-Thread geschrieben, nur lesen)
    float compGrDb = 0.0f;
    float bandGainDb[2] { 0.0f, 0.0f };
    float maxCompGrDb = 0.0f;   // größte Reduktion seit dem letzten Abholen

private:
    double sr = 48000.0;
    SvfCoeffs scHp, scLp;
    SvfState scHpS, scLpS;
    double det = 0.0, detRel = 0.0, grDb = 0.0, grLin = 1.0, attC = 0.0, relC = 0.0;

    struct Band
    {
        SvfCoeffs keyC;
        SvfState keyS;
        double det = 0.0, gainDb = 0.0, attC = 0.0, relC = 0.0, g = 0.0;
        SvfState sL, sR;
    } bands[2];
    float lastHp = -1.0f, lastLp = -1.0f;
    float lastBandF[2] { -1.0f, -1.0f }, lastBandQ[2] { -1.0f, -1.0f };
};

} // namespace samsi
