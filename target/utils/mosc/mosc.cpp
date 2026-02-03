
#include "pico/stdlib.h"

// 5351 Frequency Synthesizer library
extern "C" {
#include "si5351/si5351.h"
}

#include "si5351.h"

#include <mosc/mosc.h>

namespace MPico
{

void m_osc_init()
{
    // Initialize the Si5351; 7Mhz
    // Calibration to be done later; this is roughly correct
    si5351_init(0x60, SI5351_CRYSTAL_LOAD_8PF, 25000000, 140000); // I am using a 25 MHz TCXO

    // Just clock 0 for now
    si5351_set_clock_pwr(SI5351_CLK0, 0); // safety first
    si5351_set_clock_pwr(SI5351_CLK1, 0); // safety first
    si5351_set_clock_pwr(SI5351_CLK2, 0); // safety first

    si5351_drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351_drive_strength(SI5351_CLK1, SI5351_DRIVE_8MA);
    si5351_drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA);
}

void m_osc_set_frequency(uint64_t frequency, ClockOutput clock)
{
    int enable = 0;
    if (frequency != 0)
    {
        enable = 1;
        switch (clock)
        {
        case ClockOutput::CLOCK_0:
            si5351_set_freq(frequency * 100ULL, SI5351_CLK0);
            break;
        case ClockOutput::CLOCK_1:
            si5351_set_freq(frequency * 100ULL, SI5351_CLK1);
            break;
        case ClockOutput::CLOCK_2:
            si5351_set_freq(frequency * 100ULL, SI5351_CLK2);
            break;
        }
    }

    switch (clock)
    {
    case ClockOutput::CLOCK_0:
        si5351_output_enable(SI5351_CLK0, enable);
        si5351_set_clock_pwr(SI5351_CLK0, enable);
        return;
    case ClockOutput::CLOCK_1:
        si5351_output_enable(SI5351_CLK1, enable);
        si5351_set_clock_pwr(SI5351_CLK1, enable);
        return;
    case ClockOutput::CLOCK_2:
        si5351_output_enable(SI5351_CLK2, enable);
        si5351_set_clock_pwr(SI5351_CLK2, enable);
        return;
    }
}

} // namespace MPico