
#include "demo.h"
#include "pch.h"

#include <deque>
#include <format>

#include <zest/time/timer.h>
#include <zest/ui/colors.h>

#include <zing/audio/audio.h>

#include <implot.h>

using namespace Zing;
using namespace Zest;
using namespace std::chrono;
using namespace libremidi;

void demo_draw_analysis()
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
            std::shared_ptr<AudioAnalysisData> spNewData;
            while (pAnalysis->analysisData.try_dequeue(spNewData))
            {
                if (pAnalysis->uiDataCache)
                {
                    pAnalysis->analysisDataCache.enqueue(pAnalysis->uiDataCache);
                }
                pAnalysis->uiDataCache = spNewData;
            }

            if (!pAnalysis->uiDataCache)
            {
                continue;
            }

            const auto& spectrumBuckets = pAnalysis->uiDataCache->spectrumBuckets;
            const auto& audio = pAnalysis->uiDataCache->audio;

            if (!spectrumBuckets.empty())
            {
                if (i == 0)
                {
                    auto bucketCount = spectrumBuckets.size() / 2;
                    auto sampleCount = ctx.audioDeviceSettings.sampleRate / 2;
                    sampleCount /= uint32_t(spectrumBuckets.size());

                    static std::vector<float> xs1;
                    xs1.resize(bucketCount);
                    for (int i = 0; i < bucketCount; ++i)
                    {
                        xs1[i] = float(i * sampleCount);
                    }

                    if (ImPlot::BeginPlot(std::format("Spectrum: {}", audio_to_channel_name(Id)).c_str(), ImVec2(-1, 0), ImPlotFlags_Crosshairs | ImPlotFlags_NoLegend))
                    {
                        ImPlot::SetupAxes("Freq", "Level", ImPlotAxisFlags_Lock);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f, float(bucketCount * sampleCount), ImPlotCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0f, 1.0f, ImPlotCond_Always);
                        ImPlot::PlotLine("Level/Freq", xs1.data(), spectrumBuckets.data(), int(bucketCount));
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
                        ImPlot::SetupAxes("Sample", "Level", ImPlotAxisFlags_Lock);
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
