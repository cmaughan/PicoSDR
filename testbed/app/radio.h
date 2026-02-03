#pragma once
#include <chrono>
#include <cstdint>

void radio_process(const std::chrono::microseconds time, const float* pInput, float* pOutput, uint32_t sampleCount);

struct RadioBandpassSkirtView
{
    const float* weights = nullptr;
    uint32_t totalBins = 0;
    uint32_t passBins = 0;
    uint32_t skirtBins = 0;
    float centerIndex = 0.0f;
};

bool radio_get_bandpass_skirt(RadioBandpassSkirtView& out);
double radio_marker_center_hz();