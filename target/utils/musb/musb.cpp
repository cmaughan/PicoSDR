
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
#include <zest/logger/logger.h>

#include <pico_zest/time/pico_profiler.h>

using namespace Zest;
using namespace MPico;

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    TU_LOG1("tud_mount_cb\r\n");
    m_led_set_blink_interval(BlinkInterval::BLINK_MOUNTED);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    TU_LOG1("tud_unmount_cb\r\n");
    m_led_set_blink_interval(BlinkInterval::BLINK_NOT_MOUNTED);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    TU_LOG1("tud_suspend_cb\r\n");
    (void)remote_wakeup_en;
    m_led_set_blink_interval(BlinkInterval::BLINK_SUSPENDED);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    TU_LOG1("tud_resume_cb\r\n");
    auto interval = tud_mounted() ? BlinkInterval::BLINK_MOUNTED : BlinkInterval::BLINK_NOT_MOUNTED;
    m_led_set_blink_interval(interval);
}

namespace MPico
{


void m_usb_init()
{
    PROFILE_SCOPE(m_usb_init);

    LOG(DBG, "m_usb_init");

    board_init();

    audio_init();

    // init device stack on configured roothub port
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    board_init_after_tusb();
}

void m_usb_update()
{
    PROFILE_SCOPE(m_usb_update);

    tud_task(); // tinyusb device task
    
    m_led_blink_task();

    midi_task();
    audio_task();
    vendor_task();
}

} // namespace MPico
