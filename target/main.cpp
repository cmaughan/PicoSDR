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
#include <hardware/irq.h>

// Use the namespace for convenience
using namespace MPico;
using namespace Zest;

uint64_t frequency = 7030000;
#define I2C1_DATA 2
#define I2C1_CLOCK 3

constexpr uint32_t ADC_NUM = 0;
constexpr uint32_t ADC_PIN = 26 + ADC_NUM;
constexpr float ADC_REF = 1.0f;
constexpr float ADC_RANGE = float(1u << 12);
constexpr float ADC_SCALE = (ADC_REF / (ADC_RANGE - 1.0f));

static void adc_irq_handler()
{
    while (adc_fifo_get_level() > 0)
    {
        const uint16_t raw = adc_fifo_get();
        MPico::audio_add_sample(((raw * ADC_SCALE) - 0.5f) * 2.0f);
    }
}

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

    adc_init();
    adc_gpio_init( ADC_PIN);
    adc_select_input( ADC_NUM);

    auto audio_rate = AUDIO_SAMPLE_RATE;
    auto adc_clock = 48000000;
    auto adc_div = adc_clock / float(audio_rate);
    adc_set_clkdiv(adc_div); // sample rate
    adc_fifo_setup(
        true,  // Write each completed conversion to the sample FIFO
        false, // Do not enable DMA data request (DREQ)
        1,     // DREQ (and IRQ) asserted when at least 1 sample present
        false, // Do not enable ERR bit
        false  // Do not shift 12-bit samples to 8-bit
    );
    adc_fifo_drain();
    adc_irq_set_enabled(true);
    irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_irq_handler);
    irq_set_enabled(ADC_IRQ_FIFO, true);
    adc_run(true);

    // Update loop
    while (1)
    {
        Profiler::NewFrame();

        PROFILE_SCOPE(main_loop);

        m_usb_update();

        if (Profiler::DumpReady())
        {
            vendor_dump_profile();
        }
    }

    Profiler::Finish();
}
