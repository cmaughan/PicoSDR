// Waterfall.cpp
#include "Waterfall.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <imgui.h>
#include <implot.h>

namespace
{
inline float ToDb_FromPower(float p)
{
    constexpr float eps = 1e-20f; // power can be very small
    return 10.0f * std::log10(std::max(p, eps));
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

    wf.accumulateN = std::max(1, wf.accumulateN);
    wf.accCount = 0;
    wf.accPowerSum.assign((size_t)wf.bins, 0.0f);
}

void Waterfall_Reset(Waterfall& wf)
{
    wf.head = 0;
    wf.emaNoiseDb = -90.0f;

    std::fill(wf.ringDb.begin(), wf.ringDb.end(), -120.0f);
    std::fill(wf.uploadDb.begin(), wf.uploadDb.end(), -120.0f);

    wf.accCount = 0;
    std::fill(wf.accPowerSum.begin(), wf.accPowerSum.end(), 0.0f);
}

static void Waterfall_PushLineDb(Waterfall& wf, const float* lineDb)
{
    // Estimate noise floor using bottom 20% mean WITHOUT scrambling the stored line
    static thread_local std::vector<float> work;
    work.assign(lineDb, lineDb + wf.bins);

    int k = std::max(1, wf.bins / 5);
    std::nth_element(work.begin(), work.begin() + k, work.end());

    float sum = 0.0f;
    for (int i = 0; i < k; ++i)
        sum += work[i];
    float noiseNow = sum / (float)k;

    wf.emaNoiseDb = (1.0f - wf.noiseAlpha) * wf.emaNoiseDb + wf.noiseAlpha * noiseNow;

    float* dst = wf.ringDb.data() + (size_t)wf.head * (size_t)wf.bins;
    std::memcpy(dst, lineDb, (size_t)wf.bins * sizeof(float));

    wf.head = (wf.head + 1) % wf.rows;
}

void Waterfall_AccumulateLinePower(Waterfall& wf, const float* spectrumMag, int spectrumCount)
{
    if (!wf.enabled)
        return;
    if (wf.bins <= 0 || wf.rows <= 0)
        return;
    if (!spectrumMag)
        return;
    if (spectrumCount < wf.bins)
        return;

    wf.accumulateN = std::max(1, wf.accumulateN);
    if ((int)wf.accPowerSum.size() != wf.bins)
        wf.accPowerSum.assign((size_t)wf.bins, 0.0f);

    // Accumulate POWER = mag^2
    for (int i = 0; i < wf.bins; ++i)
    {
        float m = spectrumMag[i];
        wf.accPowerSum[i] += m * m;
    }

    wf.accCount++;
    if (wf.accCount < wf.accumulateN)
        return;

    // Average power, convert to dB(power)
    static thread_local std::vector<float> lineDb;
    lineDb.resize((size_t)wf.bins);

    float invN = 1.0f / (float)wf.accCount;
    for (int i = 0; i < wf.bins; ++i)
    {
        float pAvg = wf.accPowerSum[i] * invN;
        lineDb[i] = ToDb_FromPower(pAvg);
    }

    // Reset accumulator
    std::fill(wf.accPowerSum.begin(), wf.accPowerSum.end(), 0.0f);
    wf.accCount = 0;

    // Store the dB line
    Waterfall_PushLineDb(wf, lineDb.data());
}

void Waterfall_AccumulateLineAlreadyPower(Waterfall& wf, const float* spectrumPower, int spectrumCount)
{
    if (!wf.enabled)
        return;
    if (wf.bins <= 0 || wf.rows <= 0)
        return;
    if (!spectrumPower)
        return;
    if (spectrumCount < wf.bins)
        return;

    wf.accumulateN = std::max(1, wf.accumulateN);
    if ((int)wf.accPowerSum.size() != wf.bins)
        wf.accPowerSum.assign((size_t)wf.bins, 0.0f);

    // Accumulate power directly
    for (int i = 0; i < wf.bins; ++i)
        wf.accPowerSum[i] += spectrumPower[i];

    wf.accCount++;
    if (wf.accCount < wf.accumulateN)
        return;

    static thread_local std::vector<float> lineDb;
    lineDb.resize((size_t)wf.bins);

    float invN = 1.0f / (float)wf.accCount;
    for (int i = 0; i < wf.bins; ++i)
    {
        float pAvg = wf.accPowerSum[i] * invN;
        lineDb[i] = ToDb_FromPower(pAvg);
    }

    std::fill(wf.accPowerSum.begin(), wf.accPowerSum.end(), 0.0f);
    wf.accCount = 0;

    Waterfall_PushLineDb(wf, lineDb.data());
}

void Waterfall_BuildUpload(Waterfall& wf)
{
    if (wf.bins <= 0 || wf.rows <= 0)
        return;
    if (wf.uploadDb.size() != wf.ringDb.size())
        wf.uploadDb.resize(wf.ringDb.size());

    for (int y = 0; y < wf.rows; ++y)
    {
        int ringRow = (wf.head + y) % wf.rows; // oldest at top
        const float* src = wf.ringDb.data() + (size_t)ringRow * (size_t)wf.bins;
        float* dst = wf.uploadDb.data() + (size_t)y * (size_t)wf.bins;
        std::memcpy(dst, src, (size_t)wf.bins * sizeof(float));
    }
}

float Waterfall_FloorDb(const Waterfall& wf)
{
    return wf.emaNoiseDb + wf.noiseOffsetDb;
}

float Waterfall_CeilDb(const Waterfall& wf)
{
    return wf.emaNoiseDb + wf.dynRangeDb;
}

void Waterfall_DrawImPlot(Waterfall& wf, const char* plotTitle, float maxHz, ImVec2 plotSize)
{
    if (wf.bins <= 0 || wf.rows <= 0)
        return;

    Waterfall_BuildUpload(wf);

    const double x0 = 0.0, x1 = (double)maxHz;
    const double y0 = 0.0, y1 = (double)wf.rows;

    if (ImPlot::BeginPlot(plotTitle, plotSize, ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText))
    {

        ImPlot::SetupAxes("Freq", "Time", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, y0, y1, ImPlotCond_Always);

        ImPlot::PushColormap(ImPlotColormap_Jet);

        ImPlot::PlotHeatmap(
            "##wf",
            wf.uploadDb.data(),
            wf.rows,
            wf.bins,
            Waterfall_FloorDb(wf),
            Waterfall_CeilDb(wf),
            nullptr,
            ImPlotPoint(x0, y0),
            ImPlotPoint(x1, y1));

        ImPlot::PopColormap();
        ImPlot::EndPlot();
    }
}

