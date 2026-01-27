// Waterfall.cpp
#include "Waterfall.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// You said you're using ImGui/ImPlot.
#include <imgui.h>
#include <implot.h>

namespace {

inline float ToDb_FromPower(float p) {
    // power can be tiny
    constexpr float eps = 1e-20f;
    return 10.0f * std::log10(std::max(p, eps));
}

inline float Clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// Estimate noise floor from a dB line: mean of bottom 20% bins.
// IMPORTANT: must not scramble the stored line.
float EstimateNoiseDb_BottomMean(const float* lineDb, int bins) {
    static thread_local std::vector<float> work;
    work.assign(lineDb, lineDb + bins);

    const int k = std::max(1, bins / 5);
    std::nth_element(work.begin(), work.begin() + k, work.end());

    float sum = 0.0f;
    for (int i = 0; i < k; ++i) sum += work[i];
    return sum / float(k);
}

float ReduceNoiseWindowSpan(const float* data, int count, bool useMedian) {
    if (!data || count <= 0) return -120.0f;
    if (!useMedian) {
        float sum = 0.0f;
        for (int i = 0; i < count; ++i) sum += data[i];
        return sum / float(count);
    }

    static thread_local std::vector<float> work;
    work.assign(data, data + count);
    const size_t mid = work.size() / 2;
    std::nth_element(work.begin(), work.begin() + mid, work.end());
    if (work.size() % 2 == 1)
        return work[mid];

    const float hi = work[mid];
    std::nth_element(work.begin(), work.begin() + (mid - 1), work.end());
    const float lo = work[mid - 1];
    return 0.5f * (lo + hi);
}

// Push a fully-formed dB line into the ring buffer, and update emaNoiseDb if not locked/manual.
void PushLineDb(Waterfall& wf, const float* lineDb) {
    // Update auto noise estimate unless user says "nope"
    if (!wf.manualFloor && !wf.lockNoiseFloor) {
        const float noiseNow = EstimateNoiseDb_BottomMean(lineDb, wf.bins);
        if (wf.noiseWindowN < 1) wf.noiseWindowN = 1;
        if ((int)wf.noiseWinDb.size() != wf.noiseWindowN) {
            wf.noiseWinDb.assign(size_t(wf.noiseWindowN), noiseNow);
            wf.noiseWinHead = 0;
            wf.noiseWinCount = 0;
        }
        if (wf.noiseWinCount < wf.noiseWindowN) {
            wf.noiseWinDb[size_t(wf.noiseWinCount++)] = noiseNow;
        } else {
            wf.noiseWinDb[size_t(wf.noiseWinHead)] = noiseNow;
            wf.noiseWinHead = (wf.noiseWinHead + 1) % wf.noiseWindowN;
        }

        const int noiseCount = std::max(1, wf.noiseWinCount);
        const float noiseAvg = ReduceNoiseWindowSpan(wf.noiseWinDb.data(), noiseCount, wf.useMedianNoise);
        if (wf.noiseWinCount < wf.noiseWindowN) {
            // Bootstrap to a reasonable starting floor while window fills.
            wf.emaNoiseDb = noiseAvg;
        } else {
            const float a = Clamp(wf.adapt, 0.0f, 1.0f);
            wf.emaNoiseDb = (1.0f - a) * wf.emaNoiseDb + a * noiseAvg;
        }
    }

    // Store ordered line
    float* dst = wf.ringDb.data() + size_t(wf.head) * size_t(wf.bins);
    std::memcpy(dst, lineDb, size_t(wf.bins) * sizeof(float));

    wf.head = (wf.head + 1) % wf.rows;
    wf.rowsWritten = std::min(wf.rowsWritten + 1, wf.rows);
}

} // namespace

void Waterfall_Init(Waterfall& wf, int bins, int rows) {
    wf.bins = std::max(0, bins);
    wf.rows = std::max(1, rows);
    wf.head = 0;
    wf.rowsWritten = 0;

    wf.emaNoiseDb = -90.0f;
    wf.lockedNoiseDb = wf.emaNoiseDb;

    wf.ringDb.assign(size_t(wf.rows) * size_t(wf.bins), -120.0f);
    wf.uploadDb.assign(wf.ringDb.size(), -120.0f);

    wf.accumulateN = std::max(1, wf.accumulateN);
    wf.accCount = 0;
    wf.accPowerSum.assign(size_t(wf.bins), 0.0f);

    wf.noiseWindowN = std::max(1, wf.noiseWindowN);
    wf.noiseWinHead = 0;
    wf.noiseWinCount = 0;
    wf.noiseWinDb.assign(size_t(wf.noiseWindowN), wf.emaNoiseDb);
}

void Waterfall_Reset(Waterfall& wf) {
    wf.head = 0;
    wf.rowsWritten = 0;
    wf.emaNoiseDb = -90.0f;
    wf.lockedNoiseDb = wf.emaNoiseDb;

    std::fill(wf.ringDb.begin(), wf.ringDb.end(), -120.0f);
    std::fill(wf.uploadDb.begin(), wf.uploadDb.end(), -120.0f);

    wf.accCount = 0;
    std::fill(wf.accPowerSum.begin(), wf.accPowerSum.end(), 0.0f);

    wf.noiseWindowN = std::max(1, wf.noiseWindowN);
    wf.noiseWinHead = 0;
    wf.noiseWinCount = 0;
    wf.noiseWinDb.assign(size_t(wf.noiseWindowN), wf.emaNoiseDb);
}

void Waterfall_AccumulateMag(Waterfall& wf, const float* spectrumMag, int spectrumCount) {
    if (!wf.enabled) return;
    if (wf.bins <= 0 || wf.rows <= 0) return;
    if (!spectrumMag) return;
    if (spectrumCount < wf.bins) return;

    wf.accumulateN = std::max(1, wf.accumulateN);
    if ((int)wf.accPowerSum.size() != wf.bins)
        wf.accPowerSum.assign(size_t(wf.bins), 0.0f);

    // accumulate POWER = mag^2
    for (int i = 0; i < wf.bins; ++i) {
        const float m = spectrumMag[i];
        wf.accPowerSum[i] += m * m;
    }

    wf.accCount++;
    if (wf.accCount < wf.accumulateN)
        return;

    // Average power -> dB(power)
    static thread_local std::vector<float> lineDb;
    lineDb.resize(size_t(wf.bins));

    const float invN = 1.0f / float(wf.accCount);
    for (int i = 0; i < wf.bins; ++i) {
        const float pAvg = wf.accPowerSum[i] * invN;
        lineDb[i] = ToDb_FromPower(pAvg);
    }

    // reset accumulator
    std::fill(wf.accPowerSum.begin(), wf.accPowerSum.end(), 0.0f);
    wf.accCount = 0;

    // commit
    PushLineDb(wf, lineDb.data());
}

void Waterfall_BuildUpload(Waterfall& wf) {
    if (wf.bins <= 0 || wf.rows <= 0) return;
    if (wf.uploadDb.size() != wf.ringDb.size())
        wf.uploadDb.resize(wf.ringDb.size());

    // Newest row at bottom (y=0). Older rows move upward.
    if (wf.rowsWritten <= 0) {
        const float fill = Waterfall_FloorDb(wf);
        std::fill(wf.uploadDb.begin(), wf.uploadDb.end(), fill);
        return;
    }

    const int newestRow = (wf.head - 1 + wf.rows) % wf.rows;
    for (int y = 0; y < wf.rows; ++y) {
        if (y >= wf.rowsWritten) {
            const float fill = Waterfall_FloorDb(wf);
            float* dst = wf.uploadDb.data() + size_t(y) * size_t(wf.bins);
            std::fill(dst, dst + wf.bins, fill);
            continue;
        }
        const int ringRow = (newestRow - y + wf.rows) % wf.rows;
        const float* src = wf.ringDb.data() + size_t(ringRow) * size_t(wf.bins);
        float* dst = wf.uploadDb.data() + size_t(y) * size_t(wf.bins);
        std::memcpy(dst, src, size_t(wf.bins) * sizeof(float));
    }
}

float Waterfall_FloorDb(const Waterfall& wf) {
    if (wf.manualFloor)
        return wf.manualFloorDb;

    if (wf.lockNoiseFloor)
        return wf.lockedNoiseDb + wf.floorOffsetDb;

    return wf.emaNoiseDb + wf.floorOffsetDb;
}

float Waterfall_CeilDb(const Waterfall& wf) {
    return Waterfall_FloorDb(wf) + wf.rangeDb;
}

void Waterfall_DrawControls(Waterfall& wf) {
    // The ?radio panel? bits
    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_None)) {
        ImGui::SliderFloat("WF Range (dB)", &wf.rangeDb, 10.0f, 90.0f, "%.1f");
        ImGui::SliderFloat("WF Adapt", &wf.adapt, 0.001f, 0.30f, "%.3f");
        ImGui::SliderInt("WF Speed (spectra/row)", &wf.accumulateN, 1, 64);

        ImGui::Separator();

        ImGui::SliderFloat("WF Offset (dB)", &wf.floorOffsetDb, -40.0f, 40.0f, "%.1f");
        ImGui::SliderInt("WF Noise Window (rows)", &wf.noiseWindowN, 1, 64);
        ImGui::Checkbox("WF Noise Median", &wf.useMedianNoise);

        if (ImGui::Checkbox("WF Lock Noise", &wf.lockNoiseFloor)) {
            if (wf.lockNoiseFloor) {
                // lock current auto estimate as baseline
                wf.lockedNoiseDb = wf.emaNoiseDb;
            }
        }

        ImGui::Checkbox("WF Manual Floor", &wf.manualFloor);
        if (wf.manualFloor) {
            ImGui::SliderFloat("WF Floor (dB)", &wf.manualFloorDb, -160.0f, -10.0f, "%.1f");
        }

        if (ImGui::Button("WF Reset")) {
            Waterfall_Reset(wf);
        }

        // Optional debug readout (handy while tuning)
        ImGui::Text("AutoNoise: %.1f dB  Floor: %.1f dB  Ceil: %.1f dB",
                    wf.emaNoiseDb, Waterfall_FloorDb(wf), Waterfall_CeilDb(wf));
    }
}

void Waterfall_DrawPlot(Waterfall& wf, const char* plotTitle, float maxHz, ImVec2 plotSize) {
    if (!wf.enabled) return;
    if (wf.bins <= 0 || wf.rows <= 0) return;

    Waterfall_BuildUpload(wf);

    const double x0 = 0.0;
    const double x1 = (double)maxHz;
    const double y0 = (double)wf.rows;
    const double y1 = 0.0;

    if (ImPlot::BeginPlot(plotTitle, plotSize,
        ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText)) {

        ImPlot::SetupAxes("Freq", "Time", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, y0, y1, ImPlotCond_Always);

        // ?Classic radio-ish? (widely available in older ImPlot)
        ImPlot::PushColormap(ImPlotColormap_Jet);

        ImPlot::PlotHeatmap(
            "##wf",
            wf.uploadDb.data(),
            wf.rows, wf.bins,
            Waterfall_FloorDb(wf),
            Waterfall_CeilDb(wf),
            nullptr,
            ImPlotPoint(x0, y0),
            ImPlotPoint(x1, y1)
        );

        ImPlot::PopColormap();
        ImPlot::EndPlot();
    }
}
