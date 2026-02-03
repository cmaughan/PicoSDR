#pragma once
#include <chrono>

void radio_process(const std::chrono::microseconds time, const float* pInput, float* pOutput, uint32_t sampleCount);
