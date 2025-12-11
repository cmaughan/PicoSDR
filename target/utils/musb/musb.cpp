
#include <bsp/board_api.h>
#include <musb/tusb_config.h>
#include <tusb.h>

#include <pico/stdlib.h>

#include <cmath>

#include <mled/mled.h>
#include <musb/musb_midi.h>
#include <musb/musb_vendor.h>
#include <musb/musb_audio.h>
 
#include <zest/file/serializer.h>
#include <pico_zest/time/pico_profiler.h>

using namespace Zest;

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum
{
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;


//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    TU_LOG1("tud_mount_cb\r\n");
    blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    TU_LOG1("tud_unmount_cb\r\n");
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    TU_LOG1("tud_suspend_cb\r\n");
    (void)remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    TU_LOG1("tud_resume_cb\r\n");
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

namespace MPico
{

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
    PROFILE_SCOPE(led_blinking_task);

    static uint32_t start_ms = 0;
    static bool led_state = false;

    // Blink every interval ms
    if (board_millis() - start_ms < blink_interval_ms)
    {
        return; // not enough time
    }
    start_ms += blink_interval_ms;

    m_set_led(led_state);
    led_state = 1 - led_state; // toggle
}

void m_usb_init()
{
    PROFILE_SCOPE(m_usb_init);
    board_init();

    audio_init();

    // init device stack on configured roothub port
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    board_init_after_tusb();

    /*
  #if (CFG_TUSB_MCU == OPT_MCU_RP2040)
    stdio_init_all();
  #endif
  */

    TU_LOG1("PicoSDR USB Init!\r\n");

}

void m_usb_update()
{
    PROFILE_SCOPE(m_usb_update);

    tud_task(); // tinyusb device task
    led_blinking_task();
    midi_task();
    audio_task();
    vendor_task();
}

} // namespace MPico
