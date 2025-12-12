#include "class/vendor/vendor_device.h"
#include <bsp/board_api.h>
#include <musb/musb_vendor.h>
#include <musb/tusb_config.h>
#include <tusb.h>

#include <pico_zest/time/pico_profiler.h>
#include <zest/logger/logger.h>
 
using namespace Zest;

namespace MPico
{
void send_blob_blocking(std::string const& payload)
{
    uint32_t total = static_cast<uint32_t>(payload.size());

    // Wait for host to have configured the interface
    if (!tud_vendor_mounted())
    {
        LOG(DBG, "Vendor not mounted!");
        return;
    }

    // 1) Send length header
    {
        uint32_t len_le = total; // little-endian, RP2040 + PC both LE anyway
        uint32_t sent = 0;
        while (sent < sizeof(len_le)) {
            tud_task(); // pump USB

            uint32_t wrote = tud_vendor_write(
                reinterpret_cast<uint8_t*>(&len_le) + sent,
                sizeof(len_le) - sent);

            sent += wrote;
        }
        tud_vendor_flush();
    }

    // 2) Send payload
    uint32_t offset = 0;
    while (offset < total)
    {
        tud_task(); // VERY IMPORTANT: let TinyUSB process IN tokens

        // Optional: check if host is still configured
        if (!tud_vendor_mounted()) {
            LOG(DBG, "Vendor went away");
            break;
        }

        // How much TinyUSB says we can push right now
        uint32_t avail = tud_vendor_write_available();
        if (avail == 0) {
            // No room yet; give USB more time
            continue;
        }

        uint32_t chunk = std::min<uint32_t>(avail, total - offset);
        uint32_t wrote = tud_vendor_write(
            payload.data() + offset,
            chunk);

        offset += wrote;
    }

    tud_vendor_flush();
}

bool requested = false;
void vendor_dump_profile()
{
    if (Profiler::DumpReady() && requested)
    {
        LOG(DBG, "Dumping profile");
        auto ss = Profiler::Dump();

        std::string payload = ss.str();
        send_blob_blocking(payload);
        requested = false;
    }
}

void vendor_task()
{
    PROFILE_SCOPE(vendor_task);

    // If PC sent us something
    if (tud_vendor_available())
    {
        uint8_t buf[64];
        uint32_t count = tud_vendor_read(buf, sizeof(buf));

        if (count == 1 && buf[0] == 1)
        {
            LOG(DBG, "Requesting profile");
            Profiler::Reset();
            requested = true;
        }
    }
}

} // namespace MPico
