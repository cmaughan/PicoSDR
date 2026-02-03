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
    float outputGain = 10.0f;
    struct AgcSettings
    {
        bool enabled = true;
        float targetDb = -14.0f;
        float attack = 0.2f;
        float release = 0.02f;
    };
    AgcSettings inputAgc{};
    AgcSettings outputAgc{};
};

RadioSettings& GetRadioSettings();
void radio_settings_add_hooks();
