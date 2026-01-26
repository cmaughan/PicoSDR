// Waterfall.h
#pragma once
#include <vector>
struct ImVec2;

struct Waterfall {
    int   bins        = 0;
    int   rows        = 50;
    float noiseAlpha  = 0.08f;
    float dynRangeDb  = 60.0f;
    bool  enabled     = true;

    int   head        = 0;
    float emaNoiseDb  = -90.0f;

    std::vector<float> ringDb;
    std::vector<float> uploadDb;

    // Time accumulation
    int   accumulateN = 8;          // spectra per row
    int   accCount    = 0;
    std::vector<float> accPowerSum; // bins-sized SUM OF POWER

    float noiseOffsetDb = 0.0f; // manual bias
};

void  Waterfall_Init(Waterfall& wf, int bins, int rows = 50);
void  Waterfall_Reset(Waterfall& wf);

void  Waterfall_BuildUpload(Waterfall& wf);

float Waterfall_FloorDb(const Waterfall& wf);
float Waterfall_CeilDb (const Waterfall& wf);

// Power accumulation API (call every spectrum update)
void  Waterfall_AccumulateLinePower(Waterfall& wf, const float* spectrumMag, int spectrumCount);

// Optional: if you ever already have power bins, you can call this instead.
void  Waterfall_AccumulateLineAlreadyPower(Waterfall& wf, const float* spectrumPower, int spectrumCount);

float Waterfall_FloorDb(const Waterfall& wf);

void  Waterfall_DrawImPlot(Waterfall& wf,
                           const char* plotTitle,
                           float maxHz,
                           ImVec2 plotSize);
