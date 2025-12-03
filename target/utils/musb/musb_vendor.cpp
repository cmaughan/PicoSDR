#include <musb/tusb_config.h>
#include <tusb.h>
#include <musb/musb_vendor.h>
#include <bsp/board_api.h>

namespace MPico
{
void vendor_task()
{
    // If PC sent us something
    if (tud_vendor_available())
    {
        uint8_t buf[64];
        uint32_t count = tud_vendor_read(buf, sizeof(buf));

        // Handle incoming data...
        // e.g. echo back:
        tud_vendor_write(buf, count);
        tud_vendor_flush();
    }

    // Example: periodically send a block of data
    /*
    static uint32_t last_ms = 0;
    uint32_t now = board_millis();

    if (now - last_ms > 1000 && tud_vendor_mounted())
    {
        last_ms = now;
        uint8_t payload[64];
        for (int i = 0; i < 64; ++i) payload[i] = 98+i;

        tud_vendor_write(payload, sizeof(payload));
        tud_vendor_flush();
    }
        */
}
} // namespace MPico
