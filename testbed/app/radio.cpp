#include "radio.h"
#include "radio_settings.h"
#include <zing/audio/waterfall.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <kiss_fft.h>
#include <vector>
#include <zing/audio/audio.h>
#include <zest/algorithm/ring_buffer.h>

using namespace Zing;

namespace
{

struct RadioFftState
{
    uint32_t fftSize = 0;
    uint32_t hopDiv = 2;
    uint32_t hopSize = 0;
    kiss_fft_cfg cfgFwd = nullptr;
    kiss_fft_cfg cfgInv = nullptr;
    std::vector<float> window;
    std::vector<float> olaScale;
    std::vector<float> fftIn;
    std::vector<kiss_fft_cpx> fftInCpx;
    std::vector<kiss_fft_cpx> fftOut;
    std::vector<kiss_fft_cpx> fftShifted;
    std::vector<float> fftBins;
    std::vector<kiss_fft_cpx> ifftOut;
    Zest::ring_buffer<float> ring;
    std::vector<float> outBuffer;
    uint32_t outRead = 0;
    uint32_t outWrite = 0;

    std::vector<float> skirtWeights;
    uint32_t skirtBins = 0;
    uint32_t passBins = 0;
    uint32_t totalBins = 0;
    double cachedBinHz = 0.0;
    double cachedWidthHz = 0.0;
    float cachedFalloff = 0.0f;
    float cachedSkirtRatio = 0.0f;

    float agcGain = 1.0f;
    float agcPower = 0.0f;
    float outAgcGain = 1.0f;
    float outAgcPower = 0.0f;
    std::vector<float> outBlock;
};

RadioFftState g_fft;


std::vector<uint32_t> build_bucket_edges(uint32_t limit, uint32_t buckets)
{
    std::vector<uint32_t> edges;
    edges.reserve(buckets);

    for (uint32_t i = 0; i < buckets; ++i)
    {
        edges.push_back(uint32_t(limit * (float(i) / float(buckets))));
    }
    return edges;
}

double marker_center_bin(float markerX)
{
    auto& ctx = GetAudioContext();
    const uint32_t frames = std::max(2u, ctx.audioAnalysisSettings.frames);
    const uint32_t spectrumSamples = (frames / 2) + 1;
    const uint32_t buckets = std::max(1u, ctx.audioAnalysisSettings.spectrumBuckets);
    const auto edges = build_bucket_edges(spectrumSamples, buckets);
    float bucketPos = markerX * float(buckets);
    bucketPos = std::clamp(bucketPos, 0.0f, std::nextafter(float(buckets), 0.0f));
    const int bucketIndex = std::clamp(int(bucketPos), 0, int(buckets - 1));
    const float bucketFrac = std::clamp(bucketPos - float(bucketIndex), 0.0f, 1.0f);

    uint32_t startBin = (bucketIndex == 0) ? 1u : edges[std::max(0, bucketIndex - 1)];
    uint32_t endBin = edges[std::min<int>(bucketIndex, int(edges.size() - 1))];
    if (endBin < startBin)
        std::swap(endBin, startBin);

    const double span = std::max(1.0, double(endBin) - double(startBin));
    return double(startBin) + (double(bucketFrac) * span);
}

double marker_center_hz(float markerX)
{
    auto& ctx = GetAudioContext();
    const uint32_t frames = std::max(2u, ctx.audioAnalysisSettings.frames);
    const double centerBin = marker_center_bin(markerX);
    return centerBin * double(ctx.audioDeviceSettings.sampleRate) / double(frames);
}


void ensure_skirt_weights(double binHz, double widthHz, float skirtWidthRatio, float falloff)
{
    const double skirtWidthHz = widthHz * std::max(0.01f, skirtWidthRatio);
    const uint32_t passBins = std::max(1u, uint32_t(std::round(widthHz / binHz)));
    const uint32_t skirtBins = std::max(1u, uint32_t(std::round(skirtWidthHz / binHz)));
    const uint32_t totalBins = passBins + (2u * skirtBins);

    if (g_fft.cachedBinHz == binHz && g_fft.cachedWidthHz == widthHz && g_fft.cachedSkirtRatio == skirtWidthRatio && g_fft.cachedFalloff == falloff && g_fft.totalBins == totalBins)
        return;

    g_fft.cachedBinHz = binHz;
    g_fft.cachedWidthHz = widthHz;
    g_fft.cachedSkirtRatio = skirtWidthRatio;
    g_fft.cachedFalloff = falloff;
    g_fft.passBins = passBins;
    g_fft.skirtBins = skirtBins;
    g_fft.totalBins = totalBins;
    g_fft.skirtWeights.assign(totalBins, 0.0f);

    const float sigmaBins = std::max(1e-3f, float(skirtBins) / std::max(0.1f, falloff));
    for (uint32_t i = 0; i < totalBins; ++i)
    {
        float gain = 0.0f;
        if (i < skirtBins)
        {
            const float d = float(skirtBins - 1u - i);
            const float x = d / sigmaBins;
            gain = std::exp(-0.5f * x * x);
        }
        else if (i < skirtBins + passBins)
        {
            gain = 1.0f;
        }
        else
        {
            const uint32_t tail = i - (skirtBins + passBins);
            const float d = float(tail);
            const float x = d / sigmaBins;
            gain = std::exp(-0.5f * x * x);
        }
        g_fft.skirtWeights[i] = gain;
    }
}

void radio_fft_init(uint32_t fftSize, uint32_t segmentCount)
{
    if (fftSize < 2)
        return;
    if (fftSize % 2 == 1)
        fftSize -= 1;

    auto& ctx = GetAudioContext();

    const uint32_t segments = std::max(1u, segmentCount);
    if (fftSize % segments != 0)
    {
        fftSize -= (fftSize % segments);
    }
    if (fftSize < 2 * segments)
        return;

    if (g_fft.fftSize == fftSize && g_fft.hopDiv == segments && g_fft.cfgFwd && g_fft.cfgInv)
        return;

    if (g_fft.cfgFwd)
    {
        kiss_fft_free(g_fft.cfgFwd);
        g_fft.cfgFwd = nullptr;
    }
    if (g_fft.cfgInv)
    {
        kiss_fft_free(g_fft.cfgInv);
        g_fft.cfgInv = nullptr;
    }

    g_fft.fftSize = fftSize;
    g_fft.hopDiv = segments;
    g_fft.hopSize = std::max(1u, fftSize / segments);
    g_fft.cfgFwd = kiss_fft_alloc(int(fftSize), 0, 0, 0);
    g_fft.cfgInv = kiss_fft_alloc(int(fftSize), 1, 0, 0);
    g_fft.window.resize(fftSize);
    g_fft.olaScale.resize(fftSize, 1.0f);
    g_fft.fftIn.assign(fftSize, 0.0f);
    g_fft.fftInCpx.assign(fftSize, kiss_fft_cpx{});
    g_fft.fftOut.assign(fftSize, kiss_fft_cpx{});
    g_fft.fftShifted.assign(fftSize, kiss_fft_cpx{});
    g_fft.fftBins.assign(fftSize, 0.0f);
    g_fft.ifftOut.assign(fftSize, kiss_fft_cpx{});
    ring_buffer_init(g_fft.ring, fftSize);
    g_fft.outBuffer.assign(fftSize, 0.0f);
    g_fft.outRead = 0;
    g_fft.outWrite = 0;

    // Hann window for smooth spectral bins
    for (uint32_t i = 0; i < fftSize; ++i)
    {
        g_fft.window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f * (i / float(fftSize - 1))));
    }

    const uint32_t overlapCount = std::max(1u, segments);
    for (uint32_t i = 0; i < fftSize; ++i)
    {
        float sum = 0.0f;
        for (uint32_t k = 0; k < overlapCount; ++k)
        {
            const uint32_t idx = (i + k * g_fft.hopSize) % fftSize;
            const float w = g_fft.window[idx];
            sum += w * w;
        }
        g_fft.olaScale[i] = sum > 1e-6f ? (1.0f / sum) : 1.0f;
    }
}

void apply_bandpass_filter()
{
    PROFILE_SCOPE(apply_bandpass_filter);

    auto& ctx = GetAudioContext();
    // Band-pass using waterfall marker (center + width)
    const auto& wf = Waterfall_Get();
    const double sampleRate = double(ctx.audioDeviceSettings.sampleRate);
    const double maxHz = sampleRate * 0.5;
    const double markerCenterHz = marker_center_hz(wf.markerX);
    const double binHz = maxHz / double(g_fft.fftSize / 2);

    const double centerBin = markerCenterHz / binHz;
    const double targetCenterHz = 750.0;
    const double targetCenterBin = targetCenterHz / binHz;
    const int64_t lowSkirtBin = int64_t(std::floor(centerBin - (double(g_fft.totalBins) * 0.5)));
    const int64_t shiftBins = int64_t(std::llround(targetCenterBin - centerBin));
    const int64_t halfBins = int64_t(g_fft.fftSize / 2);

    for (uint32_t b = 0; b < g_fft.fftShifted.size(); ++b)
    {
        g_fft.fftShifted[b].r = 0.0f;
        g_fft.fftShifted[b].i = 0.0f;
    }

    const uint32_t maxBins = std::min<uint32_t>(g_fft.totalBins, uint32_t(g_fft.fftShifted.size()));
    for (uint32_t i = 0; i < maxBins; ++i)
    {
        const int64_t srcBin = lowSkirtBin + int64_t(i);
        if (srcBin < 0 || srcBin > halfBins)
            continue;

        const int64_t dstBin = srcBin + shiftBins;
        if (dstBin < 0 || dstBin > halfBins)
            continue;

        const float gain = g_fft.skirtWeights[i];
        g_fft.fftShifted[dstBin].r = g_fft.fftOut[srcBin].r * gain;
        g_fft.fftShifted[dstBin].i = g_fft.fftOut[srcBin].i * gain;
    }

    if (g_fft.fftSize > 0)
    {
        g_fft.fftShifted[0].i = 0.0f;
    }
    if (halfBins > 0 && halfBins < int64_t(g_fft.fftShifted.size()))
    {
        g_fft.fftShifted[halfBins].i = 0.0f;
    }
    for (int64_t k = 1; k < halfBins; ++k)
    {
        const int64_t mirror = int64_t(g_fft.fftSize) - k;
        g_fft.fftShifted[mirror].r = g_fft.fftShifted[k].r;
        g_fft.fftShifted[mirror].i = -g_fft.fftShifted[k].i;
    }

    g_fft.fftOut.swap(g_fft.fftShifted);
}

void apply_agc_block(const std::vector<float>& input,
                     const RadioSettings::AgcSettings& settings,
                     float& agcPower,
                     float& agcGain,
                     std::atomic<float>* powerOut,
                     std::atomic<float>* powerOutPost,
                     std::vector<float>* applyBlock)
{
    if (input.empty())
        return;
    if (!settings.enabled)
        return;

    PROFILE_SCOPE(apply_agc_block);

    const uint32_t count = uint32_t(input.size());
    double sum = 0.0;
    double maxMag = 0.0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const double sample = input[i];
        const double power = (sample * sample);
        sum += power;
        maxMag = std::max(maxMag, std::sqrt(power));
    }
    const double avgPower = sum / double(std::max(1u, count));
    if (!std::isfinite(avgPower))
        return;

    auto& ctx = GetAudioContext();
    const float sampleRate = applyBlock ? float(ctx.outputState.sampleRate) : float(ctx.inputState.sampleRate);
    const float blockSeconds = sampleRate > 0.0f ? (float(input.size()) / sampleRate) : 0.0f;
    auto ms_to_coeff = [&](float ms) {
        if (ms <= 0.0f || blockSeconds <= 0.0f)
            return 1.0f;
        const float tau = ms / 1000.0f;
        const float coeff = 1.0f - std::exp(-blockSeconds / tau);
        return std::clamp(coeff, 0.0f, 1.0f);
    };
    const float attack = ms_to_coeff(settings.attackMs);
    const float release = ms_to_coeff(settings.releaseMs);
    if (agcPower <= 0.0f)
    {
        agcPower = float(avgPower);
    }
    else
    {
        const float coeff = avgPower > agcPower ? attack : release;
        agcPower = agcPower + coeff * float(avgPower - agcPower);
    }
    if (powerOut)
    {
        powerOut->store(agcPower, std::memory_order_relaxed);
    }

    const double power = std::max<double>(agcPower, 1e-12);
    const double rms = std::sqrt(avgPower);
    const double crest = std::clamp(maxMag / std::max(rms, 1e-12), 1.0, 20.0);
    const double targetLinear = std::pow(10.0, double(settings.targetDb) / 20.0);
    double desired = targetLinear / std::sqrt(power);
    desired /= std::sqrt(crest);
    desired = std::clamp(desired, 0.05, 50.0);

    const float gainCoeff = desired < agcGain ? attack : release;
    agcGain = agcGain + gainCoeff * float(desired - agcGain);

    if (powerOutPost)
    {
        powerOutPost->store(agcPower * (agcGain * agcGain), std::memory_order_relaxed);
    }

    if (applyBlock)
    {
        for (float& sample : *applyBlock)
        {
            sample *= agcGain;
        }
    }
}

void apply_input_agc(const std::vector<float>& input)
{
    PROFILE_SCOPE(apply_input_agc);
    auto& ctx = GetAudioContext();
    apply_agc_block(input, GetRadioSettings().inputAgc, g_fft.agcPower, g_fft.agcGain, &ctx.radioAgcPower, &ctx.radioAgcPowerOut, nullptr);
}

void apply_output_agc_block(std::vector<float>& block)
{
    PROFILE_SCOPE(apply_output_agc);
    auto& ctx = GetAudioContext();
    apply_agc_block(block, GetRadioSettings().outputAgc, g_fft.outAgcPower, g_fft.outAgcGain, &ctx.radioOutAgcPower, &ctx.radioOutAgcPowerOut, &block);
}

} // namespace

bool radio_get_bandpass_skirt(RadioBandpassSkirtView& out)
{
    if (g_fft.skirtWeights.empty() || g_fft.totalBins == 0)
        return false;

    const double centerBin = marker_center_bin(Waterfall_Get().markerX);
    const double lowSkirtBin = std::floor(centerBin - (double(g_fft.totalBins) * 0.5));
    const double centerIndex = centerBin - lowSkirtBin;

    out.weights = g_fft.skirtWeights.data();
    out.totalBins = g_fft.totalBins;
    out.passBins = g_fft.passBins;
    out.skirtBins = g_fft.skirtBins;
    out.centerIndex = float(std::clamp(centerIndex, 0.0, double(std::max<uint32_t>(1u, g_fft.totalBins) - 1)));
    return true;
}

void radio_process(const std::chrono::microseconds time, const float* pInput, float* pOutput, uint32_t sampleCount)
{
    PROFILE_SCOPE(radio_process);

    auto& ctx = GetAudioContext();
    (void)time;

    assert(pInput != nullptr);
    assert(pOutput != nullptr);
    if (!pOutput || !pInput)
    {
        return;
    }

    const uint32_t fftSize = std::max(2u, ctx.audioAnalysisSettings.frames);
    const auto& settings = GetRadioSettings();
    radio_fft_init(fftSize, settings.fftHopDiv);

    const uint32_t inStride = std::max(1u, ctx.inputState.channelCount);
    const uint32_t outStride = std::max(1u, ctx.outputState.channelCount);

    for (uint32_t i = 0; i < sampleCount; i++)
    {
        const float sample = pInput[i * inStride];
        float outSample = 0.0f;
        if (!g_fft.outBuffer.empty())
        {
            outSample = g_fft.outBuffer[g_fft.outRead];
            g_fft.outBuffer[g_fft.outRead] = 0.0f;
            g_fft.outRead = (g_fft.outRead + 1) % g_fft.fftSize;
        }
        pOutput[(i * outStride)] = outSample;

        if (g_fft.cfgFwd && g_fft.cfgInv && g_fft.hopSize > 0)
        {
            ring_buffer_add(g_fft.ring, sample);

            const size_t available = ring_buffer_size(g_fft.ring);
            if (available >= g_fft.fftSize)
            {
                PROFILE_SCOPE(radio_fft_update);

                ring_buffer_assign_ordered(g_fft.ring, g_fft.fftIn, g_fft.fftSize);
                ring_buffer_drain_n(g_fft.ring, g_fft.hopSize);

                if (settings.inputAgc.enabled)
                {
                    apply_input_agc(g_fft.fftIn);
                }

                for (uint32_t n = 0; n < g_fft.fftSize; ++n)
                {
                    const float sampleIn = g_fft.fftIn[n] * g_fft.window[n] * g_fft.agcGain;
                    g_fft.fftInCpx[n].r = sampleIn;
                    g_fft.fftInCpx[n].i = 0.0f;
                }

                kiss_fft(g_fft.cfgFwd, g_fft.fftInCpx.data(), g_fft.fftOut.data());

                {
                    const double sampleRate = double(ctx.audioDeviceSettings.sampleRate);
                    const double maxHz = sampleRate * 0.5;
                    const double binHz = maxHz / double(g_fft.fftSize / 2);
                    const double markerWidthHz = std::max(1.0, double(settings.markerWidthHz));
                    const float skirtWidthRatio = std::max(0.1f, settings.skirtWidthRatio);
                    const float skirtFalloff = std::max(0.1f, settings.skirtFalloff);
                    ensure_skirt_weights(binHz, markerWidthHz, skirtWidthRatio, skirtFalloff);
                }

                if (settings.enableFilter)
                {
                    apply_bandpass_filter();
                }

                for (uint32_t b = 0; b < g_fft.fftBins.size(); ++b)
                {
                    const float real = g_fft.fftOut[b].r;
                    const float imag = g_fft.fftOut[b].i;
                    g_fft.fftBins[b] = std::sqrt(real * real + imag * imag);
                }

                kiss_fft(g_fft.cfgInv, g_fft.fftOut.data(), g_fft.ifftOut.data());

                g_fft.outBlock.resize(g_fft.fftSize);
                for (uint32_t s = 0; s < g_fft.fftSize; ++s)
                {
                    g_fft.outBlock[s] = g_fft.ifftOut[s].r * g_fft.window[s] * g_fft.olaScale[s] / float(g_fft.fftSize);
                }

                apply_output_agc_block(g_fft.outBlock);

                for (uint32_t s = 0; s < g_fft.fftSize; ++s)
                {
                    float sampleOut = g_fft.outBlock[s];
                    const uint32_t outIdx = (g_fft.outWrite + s) % g_fft.fftSize;
                
                    g_fft.outBuffer[outIdx] += sampleOut * settings.outputGain;
                }

                g_fft.outWrite = (g_fft.outWrite + g_fft.hopSize) % g_fft.fftSize;
            }
        }
    }
}
