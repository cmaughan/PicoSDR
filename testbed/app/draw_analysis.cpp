
#include "testbed.h"
#include "waterfall.h"
#include "pch.h"

#include <deque>
#include <format>

#include <zest/time/timer.h>
#include <zest/ui/colors.h>

#include <zing/audio/audio.h>

#include <implot.h>

#include "waterfall.h"

using namespace Zing;
using namespace Zest;
using namespace std::chrono;
using namespace libremidi;

void draw_analysis()
{
    PROFILE_SCOPE(demo_draw_analysis)
    auto& ctx = GetAudioContext();

    const size_t bufferWidth = 512; // default width if no data
    const auto BufferTypes = 2;     // Spectrum + Audio
    const auto BufferHeight = ctx.analysisChannels.size() * BufferTypes;

    for (uint32_t i = 0; i < 2; i++)
    {
        for (auto [Id, pAnalysis] : ctx.analysisChannels)
        {
            if (!pAnalysis->uiDataCache)
            {
                continue;
            }
        
            if (Id.first != Channel_In)
            {
                continue;
            }

            const auto& spectrumBuckets = pAnalysis->uiDataCache->spectrumBuckets;
            const auto& audio = pAnalysis->uiDataCache->audio;

            if (!spectrumBuckets.empty())
            {
                if (i == 1)
                {
                    auto bucketCount = spectrumBuckets.size() / 2;
                    auto sampleCount = ctx.audioDeviceSettings.sampleRate / 2;
                    /// 2;
                    sampleCount /= uint32_t(spectrumBuckets.size());

                    static std::vector<float> xs1;
                    xs1.resize(bucketCount);
                    for (int i = 0; i < bucketCount; ++i)
                    {
                        xs1[i] = float(i * sampleCount);
                    }

                    if (ImPlot::BeginPlot(std::format("Spectrum: {}", audio_to_channel_name(Id)).c_str(), ImVec2(-1, 0), ImPlotFlags_Crosshairs | ImPlotFlags_NoLegend))
                    {
                        ImPlot::SetupAxes("", "", ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f, float(bucketCount * sampleCount), ImPlotCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0f, 1.0f, ImPlotCond_Always);
                        ImPlot::PlotLine("Level/Freq", xs1.data(), spectrumBuckets.data(), int(bucketCount));

                        const double x0 = 0.0;
                        const double x1 = double(bucketCount * sampleCount);
                        auto& wf = Waterfall_Get();
                        const double markerValue = x0 + (x1 - x0) * std::clamp<double>(wf.markerX, 0.0, 1.0);
                        const double markerWidthHz = 500.0;
                        const double markerHalfHz = markerWidthHz * 0.5;

                        const ImPlotPoint rectMin(markerValue - markerHalfHz, 0.0);
                        const ImPlotPoint rectMax(markerValue + markerHalfHz, 1.0);
                        ImPlot::PushPlotClipRect();
                        ImPlot::GetPlotDrawList()->AddRectFilled(
                            ImPlot::PlotToPixels(rectMin),
                            ImPlot::PlotToPixels(rectMax),
                            IM_COL32(255, 255, 255, 48));
                        const ImVec2 lineMin = ImPlot::PlotToPixels(ImPlotPoint(markerValue - markerHalfHz, 0.0));
                        const ImVec2 lineMax = ImPlot::PlotToPixels(ImPlotPoint(markerValue - markerHalfHz, 1.0));
                        const ImVec2 lineMin2 = ImPlot::PlotToPixels(ImPlotPoint(markerValue + markerHalfHz, 0.0));
                        const ImVec2 lineMax2 = ImPlot::PlotToPixels(ImPlotPoint(markerValue + markerHalfHz, 1.0));
                        ImPlot::GetPlotDrawList()->AddLine(lineMin, lineMax, IM_COL32(255, 255, 0, 255), 1.0f);
                        ImPlot::GetPlotDrawList()->AddLine(lineMin2, lineMax2, IM_COL32(255, 255, 0, 255), 1.0f);
                        ImPlot::PopPlotClipRect();
                        ImPlot::EndPlot();
                    }
                }
                else
                {
                    static std::vector<float> xs1;
                    xs1.resize(audio.size());
                    for (int i = 0; i < audio.size(); ++i)
                    {
                        xs1[i] = float(i);
                    }

                    if (ImPlot::BeginPlot(std::format("Audio: {}", audio_to_channel_name(Id)).c_str(), ImVec2(-1, 0), ImPlotFlags_Crosshairs | ImPlotFlags_NoLegend))
                    {
                        ImPlot::SetupAxes("Sample", "Level", ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f, float(audio.size()), ImPlotCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0f, 1.0f, ImPlotCond_Always);
                        ImPlot::PlotLine("Sample", xs1.data(), audio.data(), int(audio.size()));
                        ImPlot::EndPlot();
                    }
                }
            }
        }
    }
}
