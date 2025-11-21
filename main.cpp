#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include <mi2c/mi2c.h>
#include <mled/mled.h>
#include <mosc/mosc.h>

// Use the namespace for convenience
using namespace MPico;

uint64_t frequency = 7000000;

#define I2C1_DATA 2
#define I2C1_CLOCK 3

int main()
{
    stdio_init_all();

    // Initialize I2C1; we have the Si5351 on the I2C1 bus
    m_i2c_init(i2c1, I2C1_CLOCK, I2C1_DATA, 100000);

    // Let things settle before we program the Si5351
    sleep_ms(250);

    m_osc_init();
    m_osc_set_frequency(frequency, ClockOutput::CLOCK_0);

    // Hello world
    m_blink(500);
    m_blink(250);

    // Update loop
    for (;;)
    {
        // Update the clock in this refresh?
        bool update_clock = false;

        // Update the clock
        if (update_clock)
        {
            m_osc_set_frequency(frequency, ClockOutput::CLOCK_0);
        }

        // Back off for now
        sleep_ms(100);
    }
}