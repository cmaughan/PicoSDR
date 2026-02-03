// App-local radio settings.
#pragma once

#include <algorithm>
#include <cmath>

#include <zest/file/toml_utils.h>
#include <zest/common.h>
#include <zest/logger/logger.h>
#include <zest/settings/settings.h>

struct RadioSettings
{
    uint32_t fftHopDiv = 2; // hop = frames / hopDiv (2 = 50% overlap)
    bool enableFilter = true;
    float markerWidthHz = 500.0f;
    float skirtWidthRatio = 0.5f;
    float skirtFalloff = 1.0f;
    float outputGain = 10.0f;
    struct AgcSettings
    {
        bool enabled = true;
        float targetDb = -14.0f;
        // Time constants in milliseconds.
        float attackMs = 50.0f;
        float releaseMs = 500.0f;
    };
    AgcSettings inputAgc{};
    AgcSettings outputAgc{};
};

RadioSettings& GetRadioSettings();
void radio_settings_add_hooks();
