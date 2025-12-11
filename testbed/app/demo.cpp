#include "pch.h"

#include "demo.h"

#include <zest/file/file.h>
#include <zest/settings/settings.h>
#include <zest/ui/layout_manager.h>

#include <zing/audio/audio.h>
#include <zing/audio/audio_analysis.h>
#include <zing/audio/midi.h>

#include <config_testbed_app.h>

#include <libremidi/reader.hpp>

#include <libusb/libusb/libusb/libusb.h>

#include <remidi/include/libremidi/libremidi.hpp>

#include <zest/file/serializer.h>
#include <zest/time/profiler_data.h>

using namespace Zing;
using namespace Zest;
using namespace std::chrono;
using namespace libremidi;

namespace
{

// Playing a note using a custom synth demo
std::atomic<bool> playingNote = false;
std::atomic<bool> playNote = false;

int radioFrequency = 440;
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

// A simple command which uses the wave table to play a note with a phaser.
// Just an example of generating custom audio
void demo_synth_note(float* pOut, uint32_t samples)
{
    auto& ctx = GetAudioContext();

    if (playNote)
    {
        // Create a wavetable and an an oscillator
        if (!osc)
        {
            sp_ftbl_create(ctx.pSP, &ft, 8192);
            sp_osc_create(&osc);

            sp_gen_triangle(ctx.pSP, ft);
            sp_osc_init(ctx.pSP, osc, ft, 0);
            osc->freq = 500;

            sp_phaser_create(&phs);
            sp_phaser_init(ctx.pSP, phs);
        }

        playNote = false;
        playingNote = true;
        noteStartTime = duration_cast<milliseconds>(timer_get_elapsed(ctx.m_masterClock));
    }
    else
    {
        if (!playingNote)
        {
            return;
        }
    }

    auto currentTime = duration_cast<milliseconds>(timer_get_elapsed(ctx.m_masterClock));
    auto expired = (currentTime - noteStartTime).count() > 1000;

    if (playingNote && expired)
    {
        playingNote = false;
        return;
    }

    for (uint32_t i = 0; i < samples; i++)
    {
        float out[2] = {0.0f, 0.0f};

        sp_osc_compute(ctx.pSP, osc, &out[0], &out[0]);
        sp_phaser_compute(ctx.pSP, phs, &out[0], &out[0], &out[0], &out[1]);

        for (uint32_t ch = 0; ch < ctx.outputState.channelCount; ch++)
        {
            *pOut++ += out[ch];
        }
    }
}

void demo_register_windows()
{
    layout_manager_register_window("profiler", "Profiler", &showProfiler);
    layout_manager_register_window("audio", "Audio State", &showAudio);
    layout_manager_register_window("settings", "Audio Settings", &showAudioSettings);
    layout_manager_register_window("debug_settings", "Debug Settings", &showDebugSettings);
    layout_manager_register_window("demo_window", "Demo Window", &showDemoWindow);

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

void demo_bulk_vendor_release()
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

void demo_bulk_vendor_init()
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

void demo_bulk_vendor_get_profile()
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

    std::vector<uint8_t> sysex {
        0xF0,        // SysEx start
        0x7D         // Educational / non-commercial manufacturer ID
    };

    // Pack 32-bit value into 7-bit-safe chunks
    sysex.push_back((value >>  0) & 0x7F);
    sysex.push_back((value >>  7) & 0x7F);
    sysex.push_back((value >> 14) & 0x7F);
    sysex.push_back((value >> 21) & 0x7F);
    sysex.push_back((value >> 28) & 0x0F); // only 4 bits left

    sysex.push_back(0xF7); // SysEx end

    midi.send_message(sysex);
}

void demo_send_frequency()
{
    send_u32_sysex(midiTarget, static_cast<uint32_t>(radioFrequency));
}

void demo_init_midi_target()
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

void demo_init()
{
    auto& ctx = GetAudioContext();

    // Lock the ticker to avoid loading conflicts (we unlock when fonts are loaded)
    //ctx.audioTickEnableMutex.lock();

    demo_register_windows();

    demo_bulk_vendor_init();

    audio_init([=](const std::chrono::microseconds hostTime, void* pOutput, std::size_t numSamples) {
        auto& ctx = GetAudioContext();
        for (uint32_t i = 0; i < numSamples * ctx.outputState.channelCount; i++)
        {
            ((float*)pOutput)[i] = 0.0f;
        }

        // Do extra audio synth work here
        demo_synth_note((float*)pOutput, uint32_t(numSamples));
    });

    demo_init_midi_target();
}

// Called outside of the the ImGui frame
void demo_tick()
{
    auto& ctx = GetAudioContext();
    layout_manager_update();
}

void demo_draw_menu()
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
                demo_bulk_vendor_get_profile();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    layout_manager_do_menu_popups();
}

void demo_draw()
{
    PROFILE_SCOPE(demo_draw)

    auto& ctx = GetAudioContext();

    demo_draw_menu();

    if (showDemoWindow)
    {
        ImGui::ShowDemoWindow(&showDemoWindow);
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

    // Link state, Audio settings, metronome, etc.
    if (showAudio)
    {
        if (ImGui::Begin("Audio", &showAudio))
        {
            ImGui::SeparatorText("Test");
            ImGui::BeginDisabled(ctx.outputState.channelCount == 0 ? true : false);

            if (ImGui::Button("Play Note"))
            {
                playNote = true;
            }

            if (ImGui::InputInt("Frequency", &radioFrequency))
            {
                if (radioFrequency < 20)
                    radioFrequency = 20;
                if (radioFrequency > 20000)
                    radioFrequency = 20000;
            }

            if (ImGui::Button("Update Frequency"))
            {
                demo_send_frequency();
            }

            bool midi = ctx.settings.enableMidi;
            if (ImGui::Checkbox("Midi Synth Out", &midi))
            {
                ctx.settings.enableMidi = midi;
            }
            ImGui::EndDisabled();

            ImGui::SeparatorText("Analysis");
            demo_draw_analysis();
        }

        ImGui::End();
    }
}

void demo_cleanup()
{
    demo_bulk_vendor_release();

    layout_manager_save();

    // Get the settings
    audio_destroy();
}
