#include "pico/stdlib.h"

#include <mled/mled.h>

#include "hardware/i2c.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

namespace
{
bool init = false;
}

namespace MPico
{

// Utility function to blind the light for debugging
int m_led_init()
{
#ifndef PICO_W
    // A device like Pico that uses a GPIO for the LED will define PICO_DEFAULT_LED_PIN
    // so we can use normal GPIO functionality to turn the led on and off
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
#else
    // For Pico W devices we need to initialise the driver etc
    return cyw43_arch_init();
#endif
}

// Turn the led on or off
void m_set_led(bool led_on)
{
    if (!init)
    {
        m_led_init();
        init = true;
    }
#ifndef PICO_W
    // Just set the GPIO on or off
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#else
    // Ask the wifi "driver" to set the GPIO on or off
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#endif
}

void m_blink(uint32_t count)
{
    m_set_led(true);
    sleep_ms(count);
    m_set_led(false);
    sleep_ms(count);
}


} // namespace MPico