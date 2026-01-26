// Waterfall.h
#pragma once

#include <vector>

// Forward decls to avoid dragging imgui/implot into every include site.
// Include <imgui.h> / <implot.h> in Waterfall.cpp (and in any TU that calls draw if needed).
struct ImVec2;

struct Waterfall
{
    // config
    int bins = 0;
    int rows = 50;            // history lines (e.g. 50)
    float noiseAlpha = 0.08f; // EMA smoothing for noise floor (0..1)
    float dynRangeDb = 50.0f; // visible range above noise floor
    bool enabled = true;

    // state
    int head = 0;              // next row to write (ring)
    float emaNoiseDb = -90.0f; // running estimate of noise floor

    // buffers
    std::vector<float> ringDb;   // rows * bins, ring layout
    std::vector<float> uploadDb; // rows * bins, chronological (oldest->newest)

    int accumulateN = 8;       // how many spectra per waterfall row (tune this)
    int accCount = 0;          // how many accumulated so far
    std::vector<float> accSum; // bins-sized sum
};

// Lifecycle
void Waterfall_Init(Waterfall& wf, int bins, int rows = 50);
void Waterfall_Reset(Waterfall& wf);

// Data ingest (call once per spectrum update)
void Waterfall_PushLine(Waterfall& wf, const float* spectrumBins, int spectrumCount);

// Call this every spectrum update. It will internally average over N calls
// and only then append a new waterfall row.
void Waterfall_AccumulateLine(Waterfall& wf, const float* spectrumBins, int spectrumCount);

// Build chronological buffer for drawing (call before drawing)
void Waterfall_BuildUpload(Waterfall& wf);

// Helpers
float Waterfall_FloorDb(const Waterfall& wf);
float Waterfall_CeilDb(const Waterfall& wf);

// Drawing (ImPlot heatmap). You pass maxHz for X-axis scaling.
void Waterfall_DrawImPlot(Waterfall& wf, const char* plotTitle, float maxHz, ImVec2 plotSize);
