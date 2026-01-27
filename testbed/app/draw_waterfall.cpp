
#include "pch.h"
#include "testbed.h"

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

namespace
{
Waterfall wf;
}

void draw_waterfall()
{
    PROFILE_SCOPE(draw_waterfall)
    auto& ctx = GetAudioContext();

    const size_t bufferWidth = 512; // default width if no data
    const auto BufferTypes = 2;     // Spectrum + Audio
    const auto BufferHeight = ctx.analysisChannels.size() * BufferTypes;

    for (auto [Id, pAnalysis] : ctx.analysisChannels)
    {
        if (Id.first != Channel_In)
        // Only show input for now
        {
            continue;
        }

        if (!pAnalysis->uiDataCache)
        {
            continue;
        }

        const auto& spectrumBuckets = pAnalysis->uiDataCache->spectrumBuckets;
        if (!spectrumBuckets.empty())
        {
            auto bucketCount = spectrumBuckets.size() / 2;
            auto sampleCount = ctx.audioDeviceSettings.sampleRate / 2;
            /// 2;
            sampleCount /= uint32_t(spectrumBuckets.size());

            if (wf.bins != bucketCount)
            {
                Waterfall_Init(wf, int(bucketCount), 50);
            }

            Waterfall_AccumulateMag(wf, spectrumBuckets.data(), int(bucketCount));

            Waterfall_DrawControls(wf);
            Waterfall_DrawPlot(wf, "Waterfall", float(bucketCount * sampleCount), ImVec2(-1, 600));
        }
    }
}
