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
        radioSettings.fftHopDiv = settings["radio_fft_hop_div"].value_or(radioSettings.fftHopDiv);
        radioSettings.enableFilter = settings["radio_enable_filter"].value_or(radioSettings.enableFilter);
        radioSettings.markerWidthHz = settings["radio_bandwidth_hz"].value_or(radioSettings.markerWidthHz);
        radioSettings.inputAgc.targetDb = settings["radio_agc_target"].value_or(radioSettings.inputAgc.targetDb);
        radioSettings.inputAgc.attack = settings["radio_agc_attack"].value_or(radioSettings.inputAgc.attack);
        radioSettings.inputAgc.release = settings["radio_agc_release"].value_or(radioSettings.inputAgc.release);
        radioSettings.inputAgc.enabled = settings["radio_agc_enabled"].value_or(radioSettings.inputAgc.enabled);
        radioSettings.outputGain = settings["radio_output_gain"].value_or(radioSettings.outputGain);
        radioSettings.outputAgc.enabled = settings["radio_out_agc_enabled"].value_or(radioSettings.outputAgc.enabled);
        radioSettings.outputAgc.targetDb = settings["radio_out_agc_target"].value_or(radioSettings.outputAgc.targetDb);
        radioSettings.outputAgc.attack = settings["radio_out_agc_attack"].value_or(radioSettings.outputAgc.attack);
        radioSettings.outputAgc.release = settings["radio_out_agc_release"].value_or(radioSettings.outputAgc.release);
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
    auto tab = toml::table{
        { "radio_fft_hop_div", int(settings.fftHopDiv) },
        { "radio_enable_filter", settings.enableFilter },
        { "radio_bandwidth_hz", settings.markerWidthHz },
        { "radio_agc_target", settings.inputAgc.targetDb },
        { "radio_agc_attack", settings.inputAgc.attack },
        { "radio_agc_release", settings.inputAgc.release },
        { "radio_agc_enabled", settings.inputAgc.enabled },
        { "radio_output_gain", settings.outputGain },
        { "radio_out_agc_enabled", settings.outputAgc.enabled },
        { "radio_out_agc_target", settings.outputAgc.targetDb },
        { "radio_out_agc_attack", settings.outputAgc.attack },
        { "radio_out_agc_release", settings.outputAgc.release },
    };

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
    auto validate_agc = [](RadioSettings::AgcSettings& agc) {
        if (agc.targetDb > 0.0f)
        {
            const float linear = std::max(agc.targetDb, 1e-6f);
            agc.targetDb = 20.0f * std::log10(linear);
        }
        agc.targetDb = std::clamp(agc.targetDb, -80.0f, 0.0f);
        agc.attack = std::clamp(agc.attack, 0.01f, 1.0f);
        agc.release = std::clamp(agc.release, 0.001f, 1.0f);
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
