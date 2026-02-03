
#include "testbed.h"
#include "pch.h"
#include "radio.h"
#include "radio_settings.h"

#include <algorithm>
#include <deque>
#include <format>

#include <zest/time/timer.h>
#include <zest/ui/colors.h>

#include <zing/audio/audio.h>
#include <zing/audio/waterfall.h>

#include <implot.h>

double radio_marker_center_hz();

using namespace Zing;
using namespace Zest;
using namespace std::chrono;
using namespace libremidi;

namespace
{
void draw_spectrum_plot(const Zing::ChannelId& Id,
                        const std::vector<float>& spectrumBuckets,
                        float sampleRate,
                        bool showFilterBox,
                        float maxHz)
{
    if (spectrumBuckets.empty())
        return;

    PROFILE_SCOPE(draw_spectrum_plot);

    const auto bucketCount = spectrumBuckets.size();
    auto sampleCount = sampleRate * 0.5f;
    sampleCount /= float(bucketCount);

    static std::vector<float> xs;
    xs.resize(bucketCount);
    for (int i = 0; i < bucketCount; ++i)
    {
        xs[i] = float(i * sampleCount);
    }

    ImVec2 plotPos(0.0f, 0.0f);
    ImVec2 plotSize(0.0f, 0.0f);
    const float fullMaxHz = float(bucketCount * sampleCount);
    const float plotMaxHz = (maxHz > 0.0f) ? std::min(maxHz, fullMaxHz) : fullMaxHz;
    if (ImPlot::BeginPlot(std::format("Spectrum: {}", audio_to_channel_name(Id)).c_str(), ImVec2(-1, 0),
        ImPlotFlags_Crosshairs | ImPlotFlags_NoLegend | ImPlotFlags_NoFrame | ImPlotFlags_NoInputs))
    {
        ImPlot::SetupAxes("", "", ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels,
            ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f, plotMaxHz, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0f, 1.0f, ImPlotCond_Always);
        plotPos = ImPlot::GetPlotPos();
        plotSize = ImPlot::GetPlotSize();
        ImPlot::PlotLine("Level/Freq", xs.data(), spectrumBuckets.data(), int(bucketCount));

        if (showFilterBox)
        {
            auto& wf = Waterfall_Get();
            const double markerValue = std::clamp<double>(radio_marker_center_hz(), 0.0, double(plotMaxHz));
            const double markerWidthHz = std::max(1.0, double(wf.markerWidthHz));
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
        }

        ImPlot::EndPlot();
    }

    if (showFilterBox && plotSize.x > 0.0f && plotSize.y > 0.0f) {
        auto& wf = Waterfall_Get();
        ImGui::SetCursorScreenPos(plotPos);
        ImGui::InvisibleButton("##spec_marker_drag", plotSize);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
            const float t = (ImGui::GetIO().MousePos.x - plotPos.x) / plotSize.x;
            wf.markerX = std::clamp(t, 0.0f, 1.0f);
        }
    }
}

void draw_audio_plot(const Zing::ChannelId& Id, const std::vector<float>& audio)
{
    if (audio.empty())
        return;
    
    PROFILE_SCOPE(draw_audio_plot);

    static std::vector<float> xs;
    xs.resize(audio.size());
    for (int i = 0; i < int(audio.size()); ++i)
    {
        xs[i] = float(i);
    }

    if (ImPlot::BeginPlot(std::format("Audio: {}", audio_to_channel_name(Id)).c_str(), ImVec2(-1, 0),
        ImPlotFlags_Crosshairs | ImPlotFlags_NoFrame | ImPlotFlags_NoLegend))
    {
        ImPlot::SetupAxes("Sample", "Level", ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels,
            ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0f, float(audio.size()), ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0f, 1.0f, ImPlotCond_Always);
        ImPlot::PlotLine("Sample", xs.data(), audio.data(), int(audio.size()));
        ImPlot::EndPlot();
    }
}
} // namespace

void draw_analysis()
{
    PROFILE_SCOPE(demo_draw_analysis)
    auto& ctx = GetAudioContext();

    const size_t bufferWidth = 512; // default width if no data
    const auto BufferTypes = 2;     // Spectrum + Audio
    const auto BufferHeight = ctx.analysisChannels.size() * BufferTypes;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for (uint32_t i = 0; i < 2; i++)
    {
        for (auto [Id, pAnalysis] : ctx.analysisChannels)
        {
            if (!pAnalysis->uiDataCache)
            {
                continue;
            }

            // Ignore right
            if (Id.second == 1)
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
                    draw_spectrum_plot(Id, spectrumBuckets, float(ctx.audioDeviceSettings.sampleRate), true, 0.0f);
                }
                else
                {
                    draw_audio_plot(Id, audio);
                }
            }
        }
    }
    ImGui::PopStyleVar(2);
}

void draw_output_analysis()
{
    PROFILE_SCOPE(demo_draw_output_analysis)
    auto& ctx = GetAudioContext();

    Zing::ChannelId outputId{};
    std::shared_ptr<AudioAnalysisData> outputData;
    for (auto [Id, pAnalysis] : ctx.analysisChannels)
    {
        if (Id.first != Channel_Out || Id.second != 0)
        {
            continue;
        }
        if (!pAnalysis->uiDataCache)
        {
            return;
        }
        outputId = Id;
        outputData = pAnalysis->uiDataCache;
        break;
    }

    if (!outputData)
        return;

    const auto& spectrumBuckets = outputData->spectrumBuckets;
    const auto& audio = outputData->audio;
    if (spectrumBuckets.empty() || audio.empty())
        return;

    ImGui::SeparatorText("Output");
    if (ImGui::BeginTable("OutputAnalysis", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        draw_audio_plot(outputId, audio);

        ImGui::TableSetColumnIndex(1);
        const float bandHz = std::max(1.0f, GetRadioSettings().markerWidthHz);
        const float plotHz = std::max(1500.0f, bandHz);
        draw_spectrum_plot(outputId, spectrumBuckets, float(ctx.audioDeviceSettings.sampleRate), false, plotHz);

        ImGui::EndTable();
    }
}
