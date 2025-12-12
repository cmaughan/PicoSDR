#pragma once

#include <cstdint>

namespace MPico
{

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum class BlinkInterval
{
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,
};

void m_led_set_blink_interval(BlinkInterval interval);
void m_led_blink(uint32_t count);
void m_led_set_led(bool on);
void m_led_blink_task();

}
