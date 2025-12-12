#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include <mi2c/mi2c.h>
#include <mled/mled.h>
#include <mosc/mosc.h>
#include <musb/musb.h>
#include <musb/musb_vendor.h>

#include <zest/logger/logger.h>

#include <pico_zest/time/pico_profiler.h>

#include <hardware/clocks.h>

// Use the namespace for convenience
using namespace MPico;
using namespace Zest;

uint64_t frequency = 7000000;
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
