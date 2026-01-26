// Waterfall.cpp
#include "Waterfall.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// You said you're using ImGui/ImPlot.
#include <imgui.h>
#include <implot.h>

namespace
{
inline float ToDb_FromMagnitude(float mag)
{
    // If your spectrum is already power, switch to 10*log10.
    constexpr float eps = 1e-12f;
    return 20.0f * std::log10(std::max(mag, eps));
}

inline float Clamp01(float x)
{
    return std::max(0.0f, std::min(1.0f, x));
}
} //namespace

void Waterfall_Init(Waterfall& wf, int bins, int rows)
{
    wf.bins = std::max(0, bins);
    wf.rows = std::max(1, rows);
    wf.head = 0;

    wf.emaNoiseDb = -90.0f;

    wf.ringDb.assign((size_t)wf.rows * (size_t)wf.bins, -120.0f);
    wf.uploadDb.assign(wf.ringDb.size(), -120.0f);

    wf.accSum.assign((size_t)wf.bins, 0.0f);
    wf.accCount = 0;
    if (wf.accumulateN < 1)
        wf.accumulateN = 1;
}

void Waterfall_Reset(Waterfall& wf) {
    wf.head = 0;
    wf.emaNoiseDb = -90.0f;
    std::fill(wf.ringDb.begin(), wf.ringDb.end(), -120.0f);
    std::fill(wf.uploadDb.begin(), wf.uploadDb.end(), -120.0f);

    std::fill(wf.accSum.begin(), wf.accSum.end(), 0.0f);
    wf.accCount = 0;
}

void Waterfall_PushLine(Waterfall& wf, const float* spectrumBins, int spectrumCount)
{
    if (!wf.enabled)
        return;
    if (wf.bins <= 0 || wf.rows <= 0)
        return;
    if (!spectrumBins)
        return;
    if (spectrumCount < wf.bins)
        return;

    // Convert to dB in correct bin order
    static thread_local std::vector<float> lineDb;
    static thread_local std::vector<float> noiseWork;
    lineDb.resize((size_t)wf.bins);
    noiseWork.resize((size_t)wf.bins);

    for (int i = 0; i < wf.bins; ++i)
    {
        lineDb[i] = ToDb_FromMagnitude(spectrumBins[i]);
    }

    // Noise estimate on a COPY so we don't scramble the stored line
    noiseWork = lineDb;

    int k = std::max(1, wf.bins / 5);
    std::nth_element(noiseWork.begin(), noiseWork.begin() + k, noiseWork.end());

    float sum = 0.0f;
    for (int i = 0; i < k; ++i)
        sum += noiseWork[i];
    float noiseNow = sum / (float)k;

    wf.emaNoiseDb = (1.0f - wf.noiseAlpha) * wf.emaNoiseDb + wf.noiseAlpha * noiseNow;

    // Store the ordered line
    float* dst = wf.ringDb.data() + (size_t)wf.head * (size_t)wf.bins;
    std::memcpy(dst, lineDb.data(), (size_t)wf.bins * sizeof(float));

    wf.head = (wf.head + 1) % wf.rows;
}

void Waterfall_AccumulateLine(Waterfall& wf, const float* spectrumBins, int spectrumCount) {
    if (!wf.enabled) return;
    if (wf.bins <= 0 || wf.rows <= 0) return;
    if (!spectrumBins) return;
    if (spectrumCount < wf.bins) return;

    if (wf.accumulateN < 1) wf.accumulateN = 1;
    if ((int)wf.accSum.size() != wf.bins) wf.accSum.assign((size_t)wf.bins, 0.0f);

    // Accumulate (note: accumulating MAGNITUDE is fine for a display; if you later
    // want “more correct”, accumulate POWER then convert to dB.)
    for (int i = 0; i < wf.bins; ++i)
        wf.accSum[i] += spectrumBins[i];

    wf.accCount++;

    if (wf.accCount < wf.accumulateN)
        return;

    // Compute average line
    static thread_local std::vector<float> avg;
    avg.resize((size_t)wf.bins);
    float invN = 1.0f / (float)wf.accCount;
    for (int i = 0; i < wf.bins; ++i)
        avg[i] = wf.accSum[i] * invN;

    // Reset accumulator
    std::fill(wf.accSum.begin(), wf.accSum.end(), 0.0f);
    wf.accCount = 0;

    // Now push the averaged line as a normal waterfall row
    Waterfall_PushLine(wf, avg.data(), wf.bins);
}

void Waterfall_BuildUpload(Waterfall& wf)
{
    if (wf.bins <= 0 || wf.rows <= 0)
        return;
    if (wf.uploadDb.size() != wf.ringDb.size())
        wf.uploadDb.resize(wf.ringDb.size());

    // Oldest row is wf.head (next to be overwritten). Display oldest at top, newest at bottom.
    for (int y = 0; y < wf.rows; ++y)
    {
        int ringRow = (wf.head + y) % wf.rows;
        const float* src = wf.ringDb.data() + (size_t)ringRow * (size_t)wf.bins;
        float* dst = wf.uploadDb.data() + (size_t)y * (size_t)wf.bins;
        std::memcpy(dst, src, (size_t)wf.bins * sizeof(float));
    }
}

float Waterfall_FloorDb(const Waterfall& wf)
{
    return wf.emaNoiseDb;
}

float Waterfall_CeilDb(const Waterfall& wf)
{
    return wf.emaNoiseDb + wf.dynRangeDb;
}

void Waterfall_DrawImPlot(Waterfall& wf, const char* plotTitle, float maxHz, ImVec2 plotSize)
{
    if (wf.bins <= 0 || wf.rows <= 0)
        return;
    if (wf.uploadDb.size() != (size_t)wf.bins * (size_t)wf.rows)
        return;

    // BuildUpload is cheap enough at 50 lines; call here if you want “one call draw”.
    // Or call Waterfall_BuildUpload() explicitly in your frame.
    Waterfall_BuildUpload(wf);

    const double x0 = 0.0;
    const double x1 = (double)maxHz;
    const double y0 = 0.0;
    const double y1 = (double)wf.rows;

    const float floorDb = Waterfall_FloorDb(wf);
    const float ceilDb = Waterfall_CeilDb(wf);

    if (ImPlot::BeginPlot(plotTitle, plotSize, ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText))
    {

        ImPlot::SetupAxes("Freq", "Time", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, y0, y1, ImPlotCond_Always);

        // “Radio-ish” built-in map. If you want exact blue->red later, we can push a custom colormap.
        ImPlot::PushColormap(ImPlotColormap_Jet);

        // Heatmap values are row-major (rows x cols). Our uploadDb is rows * bins.
        // NOTE: PlotHeatmap’s params are (values, rows, cols).
        ImPlot::PlotHeatmap(
            "##wf",
            wf.uploadDb.data(),
            wf.rows,
            wf.bins,
            floorDb,
            ceilDb,
            nullptr,
            ImPlotPoint(x0, y0),
            ImPlotPoint(x1, y1));

        ImPlot::PopColormap();
        ImPlot::EndPlot();
    }
}
