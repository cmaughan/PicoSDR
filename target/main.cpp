#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include <mi2c/mi2c.h>
#include <mled/mled.h>
#include <mosc/mosc.h>
#include <musb/musb.h>
#include <musb/musb_vendor.h>

#include <pico_zest/time/pico_profiler.h>

#include <hardware/clocks.h>

// Use the namespace for convenience
using namespace MPico;
using namespace Zest;

uint64_t frequency = 7000000;

#define I2C1_DATA 2
#define I2C1_CLOCK 3

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

    Profiler::Init();
    Profiler::SetPaused(true);

    // stdio_init_all();

    // Setup our usb state
    m_usb_init();

    // Initialize I2C1; we have the Si5351 on the I2C1 bus
    // m_i2c_init(i2c1, I2C1_CLOCK, I2C1_DATA, 100000);

    // Let things settle before we program the Si5351
    // sleep_ms(250);

    // m_osc_init();
    // m_osc_set_frequency(frequency, ClockOutput::CLOCK_0);

    // Hello world
    // m_blink(500);
    // m_blink(250);

    // Update loop
    while (1)
    {
        Profiler::NewFrame();

        PROFILE_SCOPE(main_loop);

        /*
        {
            PROFILE_SCOPE(child1);
            {
                PROFILE_SCOPE(child2);
                {
                    PROFILE_SCOPE(child3);
                }
            }
        }
        */

        // Update the clock in this refresh?
        /*
        bool update_clock = false;

        // Update the clock
        if (update_clock)
        {
            //m_osc_set_frequency(frequency, ClockOutput::CLOCK_0);
        }
            */

        m_usb_update();

        if (Profiler::DumpReady())
        {
            vendor_dump_profile();
        }
    }

    Profiler::Finish();
}
