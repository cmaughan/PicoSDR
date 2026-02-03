// Radio settings storage + settings hooks.
#include "radio_settings.h"

namespace
{
RadioSettings g_radioSettings;

RadioSettings radio_settings_load_settings(const toml::table& settings)
{
    RadioSettings radioSettings;

    if (settings.empty())
        return radioSettings;

    try
    {
        auto read_bool = [&](const char* key, bool current) {
            if (auto val = settings[key].value<bool>())
                return *val;
            return current;
        };
        auto read_float = [&](const char* key, float current) {
            if (auto val = settings[key].value<float>())
                return *val;
            return current;
        };
        auto read_u32 = [&](const char* key, uint32_t current) {
            if (auto val = settings[key].value<uint32_t>())
                return *val;
            if (auto val = settings[key].value<int64_t>())
                return uint32_t(*val);
            return current;
        };

        radioSettings.fftHopDiv = read_u32("radio_fft_hop_div", radioSettings.fftHopDiv);
        radioSettings.enableFilter = read_bool("radio_enable_filter", radioSettings.enableFilter);
        radioSettings.markerWidthHz = read_float("radio_bandwidth_hz", radioSettings.markerWidthHz);
        radioSettings.skirtWidthRatio = read_float("radio_skirt_width_ratio", radioSettings.skirtWidthRatio);
        radioSettings.skirtFalloff = read_float("radio_skirt_falloff", radioSettings.skirtFalloff);
        radioSettings.inputAgc.targetDb = read_float("radio_agc_target", radioSettings.inputAgc.targetDb);
        radioSettings.inputAgc.attackMs = read_float("radio_agc_attack", radioSettings.inputAgc.attackMs);
        radioSettings.inputAgc.releaseMs = read_float("radio_agc_release", radioSettings.inputAgc.releaseMs);
        radioSettings.inputAgc.enabled = read_bool("radio_agc_enabled", radioSettings.inputAgc.enabled);
        radioSettings.outputGain = read_float("radio_output_gain", radioSettings.outputGain);
        radioSettings.outputAgc.enabled = read_bool("radio_out_agc_enabled", radioSettings.outputAgc.enabled);
        radioSettings.outputAgc.targetDb = read_float("radio_out_agc_target", radioSettings.outputAgc.targetDb);
        radioSettings.outputAgc.attackMs = read_float("radio_out_agc_attack", radioSettings.outputAgc.attackMs);
        radioSettings.outputAgc.releaseMs = read_float("radio_out_agc_release", radioSettings.outputAgc.releaseMs);
    }
    catch (std::exception& ex)
    {
        UNUSED(ex);
        LOG(ERR, ex.what());
    }
    return radioSettings;
}

toml::table radio_settings_save_settings(const RadioSettings& settings)
{
    toml::table tab;
    tab.insert_or_assign("radio_fft_hop_div", int(settings.fftHopDiv));
    tab.insert_or_assign("radio_enable_filter", settings.enableFilter);
    tab.insert_or_assign("radio_bandwidth_hz", settings.markerWidthHz);
    tab.insert_or_assign("radio_skirt_width_ratio", settings.skirtWidthRatio);
    tab.insert_or_assign("radio_skirt_falloff", settings.skirtFalloff);
    tab.insert_or_assign("radio_agc_target", settings.inputAgc.targetDb);
    tab.insert_or_assign("radio_agc_attack", settings.inputAgc.attackMs);
    tab.insert_or_assign("radio_agc_release", settings.inputAgc.releaseMs);
    tab.insert_or_assign("radio_agc_enabled", settings.inputAgc.enabled);
    tab.insert_or_assign("radio_output_gain", settings.outputGain);
    tab.insert_or_assign("radio_out_agc_enabled", settings.outputAgc.enabled);
    tab.insert_or_assign("radio_out_agc_target", settings.outputAgc.targetDb);
    tab.insert_or_assign("radio_out_agc_attack", settings.outputAgc.attackMs);
    tab.insert_or_assign("radio_out_agc_release", settings.outputAgc.releaseMs);
    return tab;
}

void radio_settings_validate_settings(RadioSettings& settings)
{
    settings.fftHopDiv = std::clamp(settings.fftHopDiv, 1u, 8u);
    // Ensure hop div is a power of two
    if ((settings.fftHopDiv & (settings.fftHopDiv - 1)) != 0)
    {
        uint32_t v = 1;
        while (v < settings.fftHopDiv && v < 8u)
            v <<= 1;
        settings.fftHopDiv = v;
    }
    settings.markerWidthHz = std::clamp(settings.markerWidthHz, 50.0f, 3000.0f);
    settings.skirtWidthRatio = std::clamp(settings.skirtWidthRatio, 0.1f, 2.0f);
    settings.skirtFalloff = std::clamp(settings.skirtFalloff, 0.1f, 10.0f);
    auto validate_agc = [](RadioSettings::AgcSettings& agc) {
        if (agc.targetDb > 0.0f)
        {
            const float linear = std::max(agc.targetDb, 1e-6f);
            agc.targetDb = 20.0f * std::log10(linear);
        }
        agc.targetDb = std::clamp(agc.targetDb, -80.0f, 0.0f);
        agc.attackMs = std::clamp(agc.attackMs, 1.0f, 5000.0f);
        agc.releaseMs = std::clamp(agc.releaseMs, 1.0f, 5000.0f);
    };
    validate_agc(settings.inputAgc);
    validate_agc(settings.outputAgc);
    settings.outputGain = std::clamp(settings.outputGain, 0.1f, 50.0f);
}
} // namespace

RadioSettings& GetRadioSettings()
{
    return g_radioSettings;
}

void radio_settings_add_hooks()
{
    Zest::SettingsClient client;
    client.pfnLoad = [](const std::string& location, const toml::table& tbl) -> bool {
        if (location == "audio.radio")
        {
            g_radioSettings = radio_settings_load_settings(tbl);
            radio_settings_validate_settings(g_radioSettings);
            return true;
        }
        return false;
    };

    client.pfnSave = [](toml::table& tbl) {
        auto get_table = [](toml::table& parent, const char* key) -> toml::table& {
            auto node = parent.get(key);
            if (node && node->is_table())
                return *node->as_table();
            auto itr = parent.insert_or_assign(key, toml::table{});
            return *itr.first->second.as_table();
        };

        auto& audio = get_table(tbl, "audio");
        auto& radio = get_table(audio, "radio");
        auto radioSettings = radio_settings_save_settings(g_radioSettings);
        for (auto&& [key, value] : radioSettings)
        {
            radio.insert_or_assign(key, value);
        }
    };

    auto& settings = Zest::GlobalSettingsManager::Instance();
    settings.AddClient(client);
}
