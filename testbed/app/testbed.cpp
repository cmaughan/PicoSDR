#include "pch.h"

#include "testbed.h"
#include "radio.h"
#include "radio_settings.h"

#include <zing/audio/waterfall.h>

#include <zest/file/file.h>
#include <zest/settings/settings.h>
#include <zest/ui/layout_manager.h>

#include <zing/audio/audio.h>
#include <zing/audio/audio_analysis.h>
#include <zing/audio/midi.h>

#include <config_testbed_app.h>

#include <libremidi/reader.hpp>

#include <libusb/libusb/libusb.h>

#include <remidi/include/libremidi/libremidi.hpp>

#include <zest/file/serializer.h>
#include <zest/time/profiler_data.h>
#include <tinyfiledialogs/tinyfiledialogs.h>

#include <implot.h>

using namespace Zing;
using namespace Zest;
using namespace std::chrono;
using namespace libremidi;

namespace
{

int radioFrequency = 7030000;
bool sendFrequency = false;
libremidi::midi_out midiTarget;

milliseconds noteStartTime = 0ms;
sp_osc* osc = nullptr;
sp_ftbl* ft = nullptr;
sp_phaser* phs = nullptr;

bool showAudioSettings = true;
bool showAudio = true;
bool showProfiler = true;
bool showDebugSettings = false;
bool showDemoWindow = false;

std::future<void> fontLoaderFuture;
std::future<std::shared_ptr<libremidi::reader>> midiReaderFuture;

} //namespace

void register_windows()
{
    layout_manager_register_window("profiler", "Profiler", &showProfiler);
    layout_manager_register_window("audio", "Radio", &showAudio);
    layout_manager_register_window("settings", "Audio Settings", &showAudioSettings);
    layout_manager_register_window("debug_settings", "Debug Settings", &showDebugSettings);
    layout_manager_register_window("window", "Demo Window", &showDemoWindow);

    layout_manager_load_layouts_file("zing", [](const std::string& name, const LayoutInfo& info) {
        if (!info.windowLayout.empty())
        {
            ImGui::LoadIniSettingsFromMemory(info.windowLayout.c_str());
        }
    });
}

const uint32_t VID = 0xcafe;
const uint32_t PID = 0x4038;

#define EPNUM_VENDOR_IN 0x83
#define EPNUM_VENDOR_OUT 0x03

libusb_context* ctx = nullptr;
libusb_device_handle* dev = nullptr;

std::atomic<bool> quitBulkVendorThread = false;
std::thread bulkThread;

void bulk_vendor_release()
{
    quitBulkVendorThread = true;
    if (bulkThread.joinable())
    {
        bulkThread.join();
    }

    if (dev)
    {
        libusb_release_interface(dev, VID);
        libusb_close(dev);
        dev = nullptr;
    }

    if (ctx)
    {
        libusb_exit(ctx);
        ctx = nullptr;
    }
}

void bulk_vendor_init()
{
    ctx = nullptr;
    dev = nullptr;

    int r = libusb_init(&ctx);
    if (r != 0)
    {
        return;
    }

    dev = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!dev)
    {
        //std::cerr << "Could not open device\n";
        libusb_exit(ctx);
        ctx = nullptr;
        return;
    }

    r = libusb_claim_interface(dev, 4);
    if (r != 0)
    {
        //std::cerr << "Failed to claim interface: " << r << "\n";
        libusb_close(dev);
        libusb_exit(ctx);
        ctx = nullptr;
        dev = nullptr;
        return;
    }

    bulkThread = std::move(std::thread([]() {
        std::vector<uint8_t> data;
        while (ctx && dev)
        {
            uint32_t sz;
            int got;
            auto ret = libusb_bulk_transfer(dev, EPNUM_VENDOR_IN, (unsigned char*)&sz, sizeof(sz), &got, 1000);
            if (ret == 0 && sz != 0)
            {
                data.clear();

                uint32_t required = sz;
                std::vector<uint8_t> buffer;
                buffer.resize(64);
                while (sz != 0)
                {
                    auto req = std::min(uint32_t(64), sz);

                    ret = libusb_bulk_transfer(dev, EPNUM_VENDOR_IN, (unsigned char*)buffer.data(), int(req), &got, 1000);
                    if (got == 0)
                    {
                        break;
                    }
                    sz -= got;
                    data.insert(data.end(), buffer.begin(), buffer.begin() + got);
                }

                if (data.size() == required)
                {
                    auto profileData = std::make_shared<Profiler::ProfilerData>();

                    Zest::binary_reader reader(data);

                    deserialize(reader, *profileData);

                    std::map<uint64_t, const char*> mapStrings;
                    for (uint32_t id = 0; id < profileData->stringPointers.size(); id++)
                    {
                        mapStrings[profileData->stringPointers[id]] = profileData->strings[id].c_str();
                    }

                    for (auto& thread : profileData->threadData)
                    {
                        for (auto& entry : thread.entries)
                        {
                            entry.szFile = mapStrings[entry.oldFilePointer];
                            entry.szSection = mapStrings[entry.oldSectionPointer];
                        }
                    }

                    Profiler::UnDump(profileData);
                }
            }

            // Thread sleep
            if (quitBulkVendorThread)
            {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }));
}

void bulk_vendor_get_profile()
{
    if (ctx && dev)
    {
        uint8_t out[1] = {1};
        auto ret = libusb_bulk_transfer(dev, EPNUM_VENDOR_OUT, (unsigned char*)&out[0], 1, nullptr, 1000);
    }
}

void send_u32_sysex(libremidi::midi_out& midi, uint32_t value)
{
    // SysEx format:
    // F0 <manufacturer id> <payload...> F7
    // We'll use a fake educational manufacturer ID: 0x7D (non-commercial)

    std::vector<uint8_t> sysex{
        0xF0, // SysEx start
        0x7D  // Educational / non-commercial manufacturer ID
    };

    // Pack 32-bit value into 7-bit-safe chunks
    sysex.push_back((value >> 0) & 0x7F);
    sysex.push_back((value >> 7) & 0x7F);
    sysex.push_back((value >> 14) & 0x7F);
    sysex.push_back((value >> 21) & 0x7F);
    sysex.push_back((value >> 28) & 0x0F); // only 4 bits left

    sysex.push_back(0xF7); // SysEx end

    midi.send_message(sysex);
}

void send_frequency()
{
    send_u32_sysex(midiTarget, static_cast<uint32_t>(radioFrequency));
}

void init_midi_target()
{
    libremidi::observer obs;
    auto ports = obs.get_output_ports();
    for (auto& port : ports)
    {
        if (port.port_name.find("Pico MIDI") != std::string::npos)
        {
            libremidi::midi_out midi;
            midi.open_port(port);
            midiTarget = std::move(midi);
            return;
        }
    }
}

void init()
{
    auto& ctx = GetAudioContext();

    // Lock the ticker to avoid loading conflicts (we unlock when fonts are loaded)
    //ctx.audioTickEnableMutex.lock();

    register_windows();

    bulk_vendor_init();

    audio_init([=](auto hostTime, auto pInput, auto pOutput, auto numSamples) {
        auto& ctx = GetAudioContext();

        float* inputBuffer = (float*)pInput;
        float* outputBuffer = (float*)pOutput;
        if (inputBuffer && outputBuffer)
        {
            for (uint32_t i = 0; i < std::max(ctx.inputState.channelCount, ctx.outputState.channelCount); i++)
            {
                if (i == 0)
                {
                    radio_process(hostTime, inputBuffer, outputBuffer, numSamples);
                }
                else
                {
                    /*
                    // Just mix it
                    for (unsigned long index = 0; index < numSamples; index++)
                    {
                        auto sample = ((const float*)inputBuffer)[index];

                        // Mix input to output
                        ((float*)outputBuffer)[(index * ctx.outputState.channelCount) + i] = sample;
                    }
                    */
                }
            }
        }
        else if (outputBuffer)
        {
            for (uint32_t i = 0; i < std::max(ctx.inputState.channelCount, ctx.outputState.channelCount); i++)
            {
                for (unsigned long index = 0; index < numSamples; index++)
                {
                    ((float*)outputBuffer)[(index * ctx.outputState.channelCount) + i] = 0.0f;
                }
            }
        }
    });

    init_midi_target();
}

// Called outside of the the ImGui frame
void tick()
{
    auto& ctx = GetAudioContext();
    layout_manager_update();
}

void draw_menu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Window"))
        {
            layout_manager_do_menu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Pico"))
        {
            if (ImGui::MenuItem("Get Profile Pico"))
            {
                bulk_vendor_get_profile();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    layout_manager_do_menu_popups();
}

void update_analysis()
{
    auto& ctx = GetAudioContext();
    for (auto [Id, pAnalysis] : ctx.analysisChannels)
    {
        // analysisDataCache-> <analysis thread> -> analysisData -> <ui thread> -> uiDataCache
        // Find any data from the analysis thread
        std::shared_ptr<AudioAnalysisData> spNewData;
        while (pAnalysis->analysisData.try_dequeue(spNewData))
        {
            // If we cached data, then return it to the pool
            if (pAnalysis->uiDataCache)
            {
                // Return to the pool
                pAnalysis->analysisDataCache.enqueue(pAnalysis->uiDataCache);
            }

            // New copy
            pAnalysis->uiDataCache = spNewData;
        }
    }
}

void draw()
{
    PROFILE_SCOPE(draw)

    auto& ctx = GetAudioContext();

    draw_menu();

    if (showDemoWindow)
    {
        ImGui::ShowDemoWindow(&showDemoWindow);
        ImPlot::ShowDemoWindow();
    }

    // Settings
    if (showDebugSettings)
    {
        Zest::GlobalSettingsManager::Instance().DrawGUI("Settings", &showDebugSettings);
    }

    // Profiler
    if (showProfiler)
    {
        if (ImGui::Begin("Profiler", &showProfiler))
        {
            Zest::Profiler::ShowProfile();
        }
        ImGui::End();
    }

    // Audio and link settings
    if (showAudioSettings)
    {
        if (ImGui::Begin("Audio Settings", &showAudioSettings))
        {
            audio_show_settings_gui();
        }
        ImGui::End();
    }

    update_analysis();

    // Link state, Audio settings, metronome, etc.
    if (showAudio)
    {
        if (ImGui::Begin("Radio", &showAudio))
        {
            ImGui::SeparatorText("Test");
            ImGui::BeginDisabled(ctx.outputState.channelCount == 0 ? true : false);

            auto oldF = radioFrequency;
            auto updateRF = [&]() {
                if (radioFrequency < 7000000)
                    radioFrequency = 7000000;
                if (radioFrequency > 7300000)
                    radioFrequency = 7300000;

                if (oldF != radioFrequency)
                {
                    send_frequency();
                }
            };
            if (ImGui::SliderInt("Frequency", &radioFrequency, 7000000, 7300000))
            {
                updateRF();
            }
            if (ImGui::Button("10-"))
            {
                radioFrequency -= 10;
                updateRF();
            }
            ImGui::SameLine();
            if (ImGui::Button("10+"))
            {
                radioFrequency += 10;
                updateRF();
            }
            if (ImGui::Button("-"))
            {
                radioFrequency -= 1;
                updateRF();
            }
            ImGui::SameLine();
            if (ImGui::Button("+"))
            {
                radioFrequency += 1;
                updateRF();
            }

            if (ImGui::Button("Update Frequency"))
            {
                send_frequency();
            }

            ImGui::EndDisabled();

            if (ImGui::Button("Save Input"))
            {
                // Use ImGui to open a file dialog
                // Launch the dialog and ask for a path:

                char const* lFilterPatterns[1] = {"*.sdr"};
                auto pTarget = tinyfd_saveFileDialog("Save Input As", "c:/cw.sdr", 1, lFilterPatterns, "SDR CW Files");
                if (pTarget != nullptr)
                {
                    for (auto& [ch, pAnalysis] : ctx.analysisChannels)
                    {
                        if (ch.first == Channel_In)
                        {
                            pAnalysis->inputDumpPath = fs::path(pTarget);
                            break;
                        }
                    }
                }
            }

            if (ImGui::Button("Load Input"))
            {
                // Use ImGui to open a file dialog
                // Launch the dialog and ask for a path:

                char const* lFilterPatterns[1] = {"*.sdr"};
                auto pTarget = tinyfd_openFileDialog("Open Input", "c:/cw.sdr", 1, lFilterPatterns, "SDR CW Files", false);
                if (pTarget != nullptr)
                {
                    // Load the file into a vector<float> buffer
                    auto filePath = fs::path(pTarget);
                    auto input = file_read(filePath);
                    ctx.inputStreamOverride.resize(input.size() / 4);
                    ctx.inputStreamIndex = 0;
                    memcpy(ctx.inputStreamOverride.data(), input.data(), input.size());
                }
            }

            if (ctx.audioDeviceSettings.enableInput)
            {
                auto& radioSettings = GetRadioSettings();
                Waterfall_Get().markerWidthHz = radioSettings.markerWidthHz;
                auto agc_bar = [](float power) {
                    const float db = 10.0f * std::log10(std::max(power, 1e-12f));
                    return std::clamp((db + 80.0f) / 80.0f, 0.0f, 1.0f);
                };
                if (ImGui::CollapsingHeader("Band Pass Filter", ImGuiTreeNodeFlags_None))
                {
                    int hopDivOptions[] = {1, 2, 4, 8};
                    int hopDivIndex = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        if (radioSettings.fftHopDiv == uint32_t(hopDivOptions[i]))
                        {
                            hopDivIndex = i;
                            break;
                        }
                    }
                    if (ImGui::Combo("FFT Hop Div##bandpass_hop", &hopDivIndex, "1\0002\0004\0008\000"))
                    {
                        radioSettings.fftHopDiv = uint32_t(hopDivOptions[hopDivIndex]);
                    }

                    bool enableRadioFilter = radioSettings.enableFilter;
                    if (ImGui::Checkbox("FFT Filter##bandpass_filter", &enableRadioFilter))
                    {
                        radioSettings.enableFilter = enableRadioFilter;
                    }

                    float markerWidth = radioSettings.markerWidthHz;
                    if (ImGui::SliderFloat("Bandwidth (Hz)##bandpass_width", &markerWidth, 50.0f, 3000.0f, "%.0f"))
                    {
                        radioSettings.markerWidthHz = markerWidth;
                        Waterfall_Get().markerWidthHz = markerWidth;
                    }

                    float outputGain = radioSettings.outputGain;
                    if (ImGui::SliderFloat("Output Gain##bandpass_output", &outputGain, 0.1f, 50.0f, "%.2f"))
                    {
                        radioSettings.outputGain = outputGain;
                    }

                    float skirtFalloff = radioSettings.skirtFalloff;
                    if (ImGui::SliderFloat("Skirt Falloff##bandpass_skirt_falloff", &skirtFalloff, 0.1f, 10.0f, "%.2f"))
                    {
                        radioSettings.skirtFalloff = skirtFalloff;
                    }

                    float skirtWidthRatio = radioSettings.skirtWidthRatio;
                    if (ImGui::SliderFloat("Skirt Width##bandpass_skirt_width", &skirtWidthRatio, 0.1f, 2.0f, "%.2f"))
                    {
                        radioSettings.skirtWidthRatio = skirtWidthRatio;
                    }

                    RadioBandpassSkirtView skirtView{};
                    if (radio_get_bandpass_skirt(skirtView) && skirtView.totalBins > 0)
                    {
                        static std::vector<float> xs;
                        xs.resize(skirtView.totalBins);
                        for (uint32_t i = 0; i < skirtView.totalBins; ++i)
                        {
                            xs[i] = float(i);
                        }

                        if (ImPlot::BeginPlot("Skirt##bandpass_skirt", ImVec2(-1, 80),
                            ImPlotFlags_NoLegend | ImPlotFlags_NoFrame | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs | ImPlotFlags_NoTitle))
                        {
                            ImPlot::SetupAxes("", "", ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_Lock);
                            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, double(std::max<uint32_t>(1u, skirtView.totalBins - 1)), ImPlotCond_Always);
                            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.05, ImPlotCond_Always);
                            ImPlot::PlotShaded("Skirt", xs.data(), skirtView.weights, int(skirtView.totalBins), 0.0f);
                            const float centerX = skirtView.centerIndex;
                            const float lineX[2] = {centerX, centerX};
                            const float lineY[2] = {0.0f, 1.05f};
                            ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 255, 255, 200));
                            ImPlot::PlotLine("Center", lineX, lineY, 2);
                            ImPlot::PopStyleColor();
                            ImPlot::EndPlot();
                        }
                    }
                }

                if (ImGui::CollapsingHeader("Input AGC", ImGuiTreeNodeFlags_None))
                {
                    bool agcEnabled = radioSettings.inputAgc.enabled;
                    if (ImGui::Checkbox("Enabled##input_agc_enabled", &agcEnabled))
                    {
                        radioSettings.inputAgc.enabled = agcEnabled;
                    }

                    float agcTarget = radioSettings.inputAgc.targetDb;
                    if (ImGui::SliderFloat("Target (dB)##input_agc_target", &agcTarget, -80.0f, 0.0f, "%.1f"))
                    {
                        radioSettings.inputAgc.targetDb = agcTarget;
                    }
                    float agcAttack = radioSettings.inputAgc.attackMs;
                    if (ImGui::SliderFloat("Attack (ms)##input_agc_attack", &agcAttack, 1.0f, 2000.0f, "%.1f"))
                    {
                        radioSettings.inputAgc.attackMs = agcAttack;
                    }
                    float agcRelease = radioSettings.inputAgc.releaseMs;
                    if (ImGui::SliderFloat("Release (ms)##input_agc_release", &agcRelease, 1.0f, 5000.0f, "%.1f"))
                    {
                        radioSettings.inputAgc.releaseMs = agcRelease;
                    }

                    const float agcPower = ctx.radioAgcPower.load(std::memory_order_relaxed);
                    const float agcPowerOut = ctx.radioAgcPowerOut.load(std::memory_order_relaxed);
                    const float agcPowerDb = 10.0f * std::log10(std::max(agcPower, 1e-12f));
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(255, 215, 0, 255));
                    ImGui::ProgressBar(agc_bar(agcPower), ImVec2(-1.0f, 6.0f), "");
                    ImGui::PopStyleColor();
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(0, 200, 0, 255));
                    ImGui::ProgressBar(agc_bar(agcPowerOut), ImVec2(-1.0f, 6.0f), "");
                    ImGui::PopStyleColor();
                    ImGui::Text("Power (dB): %.1f", agcPowerDb);
                }

                if (ImGui::CollapsingHeader("Output AGC", ImGuiTreeNodeFlags_None))
                {
                    bool outAgcEnabled = radioSettings.outputAgc.enabled;
                    if (ImGui::Checkbox("Enabled##output_agc_enabled", &outAgcEnabled))
                    {
                        radioSettings.outputAgc.enabled = outAgcEnabled;
                    }
                    float outAgcTarget = radioSettings.outputAgc.targetDb;
                    if (ImGui::SliderFloat("Target (dB)##output_agc_target", &outAgcTarget, -80.0f, 0.0f, "%.1f"))
                    {
                        radioSettings.outputAgc.targetDb = outAgcTarget;
                    }
                    float outAgcAttack = radioSettings.outputAgc.attackMs;
                    if (ImGui::SliderFloat("Attack (ms)##output_agc_attack", &outAgcAttack, 1.0f, 2000.0f, "%.1f"))
                    {
                        radioSettings.outputAgc.attackMs = outAgcAttack;
                    }
                    float outAgcRelease = radioSettings.outputAgc.releaseMs;
                    if (ImGui::SliderFloat("Release (ms)##output_agc_release", &outAgcRelease, 1.0f, 5000.0f, "%.1f"))
                    {
                        radioSettings.outputAgc.releaseMs = outAgcRelease;
                    }
                    const float outAgcPower = ctx.radioOutAgcPower.load(std::memory_order_relaxed);
                    const float outAgcPowerOut = ctx.radioOutAgcPowerOut.load(std::memory_order_relaxed);
                    const float outAgcPowerDb = 10.0f * std::log10(std::max(outAgcPower, 1e-12f));
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(255, 215, 0, 255));
                    ImGui::ProgressBar(agc_bar(outAgcPower), ImVec2(-1.0f, 6.0f), "");
                    ImGui::PopStyleColor();
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(0, 200, 0, 255));
                    ImGui::ProgressBar(agc_bar(outAgcPowerOut), ImVec2(-1.0f, 6.0f), "");
                    ImGui::PopStyleColor();
                    ImGui::Text("Power (dB): %.1f", outAgcPowerDb);
                }
            }

        }

        ImGui::End();

        if (ImGui::Begin("Waterfall", &showAudio))
        {
            draw_analysis();
            draw_waterfall();
            draw_output_analysis();
        }
        ImGui::End();
    }
}

void cleanup()
{
    bulk_vendor_release();

    layout_manager_save();

    // Get the settings
    audio_destroy();
}
