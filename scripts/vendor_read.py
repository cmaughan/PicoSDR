import usb.core
import usb.util
import usb.backend.libusb1
from usb.backend import libusb1

be = libusb1.get_backend(find_library=lambda x: "./libusb-1.0.dll")

usb_devices = usb.core.find(backend=be, find_all=True)
def enumerate_usb():  # I use a simple function that scans all known USB connections and saves their info in the file
    with open("EnumerateUSBLog.txt", "w") as wf:
        for i, d in enumerate(usb_devices):
            try:
                wf.write(f"USB Device number {i}:\n")
                wf.write(d._get_full_descriptor_str() + "\n")
                #wf.write(d.get_active_configuration() + "\n")
                wf.write("\n")
            except NotImplementedError:
                wf.write(f"Device number {i} is busy.\n\n")
            except usb.core.USBError:
                wf.write(f"Device number {i} is either disconnected or not found.\n\n")

enumerate_usb()


VID = 0xcafe   # your Pico's VID
PID = 0x4038  # your Pico's PID

dev = usb.core.find(backend=be, idVendor=VID, idProduct=PID)
if dev is None:
    raise ValueError("Device not found")

## Detach kernel driver if claimed (Linux)
#if dev.is_kernel_driver_active(1):  # interface index of vendor iface
#    dev.detach_kernel_driver(1)

dev.set_configuration()
cfg = dev.get_active_configuration()
intf = cfg[(4, 0)]   # interface #1, alternate setting 0 (our vendor iface)

# Find endpoints (match your endpoint addresses)
ep_out = usb.util.find_descriptor(
    intf,
    custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
)
assert ep_out is not None

ep_in = usb.util.find_descriptor(
    intf,
    custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
)
assert ep_in is not None

# Write some bytes
data_out = bytes([4, 2, 3, 4])
ep_out.write(data_out)

# Read some bytes (timeout in ms)
data_in = ep_in.read(64, timeout=1000)
print("Got:", bytes(data_in))
