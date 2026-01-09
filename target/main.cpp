#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include <mi2c/mi2c.h>
#include <mled/mled.h>
#include <mosc/mosc.h>
#include <musb/musb.h>
#include <musb/musb_vendor.h>
#include <musb/musb_audio.h>

#include <zest/logger/logger.h>

#include <pico_zest/time/pico_profiler.h>

#include <hardware/clocks.h>
#include <hardware/adc.h>

// Use the namespace for convenience
using namespace MPico;
using namespace Zest;

uint64_t frequency = 7030000;
#define I2C1_DATA 2
#define I2C1_CLOCK 3

namespace Zest
{

#undef ERROR
#ifndef NDEBUG 
Logger logger = {true, LT::DBG};
#else
Logger logger = {true, LT::INFO};
#endif
bool Log::disabled = false;

} //namespace Zest

int main()
{
    //set_sys_clock_khz(240000, true); 
    Profiler::ProfileSettings settings; 

    settings.MaxThreads = 2;
    settings.MaxCallStack = 10;
    settings.MaxEntriesPerThread = 100;
    settings.MaxFrames = 10;
    settings.MaxRegions = 10; 
    Profiler::SetProfileSettings(settings);

    LOG(DBG, "Initializing Profiler");
    Profiler::Init();
    Profiler::SetPaused(true);

    // Initialize I2C1; we have the Si5351 on the I2C1 bus
    LOG(DBG, "Initializing I2C");
    m_i2c_init(i2c1, I2C1_CLOCK, I2C1_DATA, 100000);

    // Setup our usb state
    LOG(DBG, "Initializing USB");
    m_usb_init();

    // SI5351 Oscillator
    LOG(DBG, "Initializing Oscillator");
    m_osc_init();
    m_osc_set_frequency(frequency, ClockOutput::CLOCK_0);

#define ADC_NUM 0
#define ADC_PIN (26 + ADC_NUM)
#define ADC_REF 1.0f
#define ADC_RANGE (1 << 12)
#define ADC_SCALE (ADC_REF / (ADC_RANGE - 1))

    adc_init();
    adc_gpio_init( ADC_PIN);
    adc_select_input( ADC_NUM);

    auto audio_rate = AUDIO_SAMPLE_RATE;
    auto adc_clock = 48000000;
    auto adc_div = adc_clock / float(audio_rate);
    adc_set_clkdiv(adc_div); // fastest

    // Update loop
    while (1)
    {
        Profiler::NewFrame();

        PROFILE_SCOPE(main_loop);

        audio_add_sample(((adc_read() * ADC_SCALE) - 0.5f) * 2.0f); // scale to -1.0 to +1.0

        m_usb_update();

        if (Profiler::DumpReady())
        {
            vendor_dump_profile();
        }
    }

    Profiler::Finish();
}
