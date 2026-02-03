#include "pico/stdlib.h"

#include "mi2c/mi2c.h"

namespace MPico
{

void m_i2c_init(i2c_inst_t* port, uint32_t clock, uint32_t data, uint32_t baud)
{
    // Initialize I2C0
    i2c_init(port, baud); // 100 kHz standard mode

    // Set up pins 0 and 1 for I2C, pull both up internally
    gpio_set_function(clock, GPIO_FUNC_I2C);
    gpio_set_function(data, GPIO_FUNC_I2C);
    gpio_pull_up(clock);
    gpio_pull_up(data);
    gpio_set_dir(clock, GPIO_IN);
    gpio_set_dir(data, GPIO_IN);
}

} // namespace MPico