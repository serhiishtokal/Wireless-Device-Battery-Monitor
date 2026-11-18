#include "../src/connection_refresh.h"
#include "../src/drivers/razer_link.h"
#include "../src/hid.h"

#include <cassert>

int main() {
    constexpr unsigned minute = 60000;
    const auto first = BluetoothConnectionKey(1);
    const std::wstring second = L"razer:00AB";
    ConnectionRefresh refresh;
    DeviceStatus reading;
    reading.bluetoothAddress = 1;
    reading.kind = DeviceKind::Headset;
    assert(BatteryReadingPending(reading));

    // A Bluetooth property can be missing, invalid, or still hold an old reading.
    refresh.ConnectionChanged(first, true, 100);
    assert(refresh.NextWait({}, 100, 150, minute) == 950);
    assert(refresh.NextWait({reading}, 1100, 1150, minute) == 1950);
    reading.percent = 255;
    assert(refresh.NextWait({reading}, 3100, 3150, minute) == minute);
    assert(refresh.NextWait({}, 3200, 3200, minute) == minute);

    // Only a live reading from the connecting device cancels its retries. Zero is valid.
    refresh.ConnectionChanged(first, true, 4000);
    refresh.ConnectionChanged(second, true, 4500);
    reading.percent = 0;
    assert(!BatteryReadingPending(reading));
    reading.asleep = true;
    assert(refresh.NextWait({reading}, 4500, 4500, minute) == 500);
    reading.asleep = false;
    reading.connected = false;
    DeviceStatus disconnected = reading;
    disconnected.percent = -1;
    assert(!BatteryReadingPending(disconnected));
    assert(refresh.NextWait({reading}, 4700, 4700, minute) == 300);
    reading.connected = true;
    assert(refresh.NextWait({reading}, 5000, 5000, minute) == 500);
    refresh.ConnectionChanged(second, false, 5300);
    assert(refresh.NextWait({}, 5300, 5300, minute) == minute);

    // A slow scan can pass both deadlines. Run one follow-up without queueing another.
    refresh.ConnectionChanged(first, true, 6000);
    assert(refresh.NextWait({}, 6000, 9500, minute) == 0);
    assert(refresh.NextWait({}, 9500, 9600, minute) == minute);

    // Reconnecting gets a new retry window. Duplicate connect notifications keep its deadline.
    refresh.ConnectionChanged(first, true, 10000);
    refresh.ConnectionChanged(first, false, 10100);
    refresh.ConnectionChanged(first, true, 10200);
    refresh.ConnectionChanged(first, true, 10300);
    assert(refresh.NextWait({}, 10300, 10300, minute) == 900);
    assert(refresh.NextWait({reading}, 11200, 11200, minute) == minute);

    // Reports captured with the receiver left plugged in as the mouse powered off and on.
    const std::uint8_t off[16] = { 0x05, 0x09, 0x02, 0x01 };
    const std::uint8_t on[16] = { 0x05, 0x09, 0x03, 0x01 };
    const std::uint8_t startup[16] = { 0x05, 0x31, 0xFF, 0, 0, 1 };
    const std::uint8_t wrongChannel[16] = { 0x04, 0x09, 0x03, 0x01 };
    const std::uint8_t unknown[16] = { 0x05, 0x09, 0x04, 0x01 };
    assert(ParseRazerLinkReport(off, sizeof off) == RazerLinkChange::Disconnected);
    assert(ParseRazerLinkReport(on, sizeof on) == RazerLinkChange::Connected);
    assert(ParseRazerLinkReport(on, sizeof on - 1) == RazerLinkChange::None);
    assert(ParseRazerLinkReport(startup, sizeof startup) == RazerLinkChange::None);
    assert(ParseRazerLinkReport(wrongChannel, sizeof wrongChannel) == RazerLinkChange::None);
    assert(ParseRazerLinkReport(unknown, sizeof unknown) == RazerLinkChange::None);

    DeviceStatus mouse;
    mouse.key = second;
    mouse.kind = DeviceKind::Mouse;
    assert(BatteryReadingPending(mouse));
    refresh.ConnectionChanged(second, true, 12000);
    assert(refresh.NextWait({mouse}, 12000, 12050, minute) == 950);
    mouse.percent = 80;
    assert(refresh.NextWait({mouse}, 13000, 13050, minute) == minute);

    assert(UsbHidProductId(LR"(\\?\HID#VID_1532&PID_00AB&MI_01&COL02#123)") == 0x153200AB);
    assert(UsbHidProductId(LR"(\\?\hid#vid_046d&pid_b012#123)") == 0x046DB012);
    assert(UsbHidProductId(LR"(\\?\hid#vid_1532&pid_00az#123)") == 0);
    assert(UsbHidProductId(LR"(\\?\hid#{00001812-0000-1000-8000-00805f9b34fb}_dev_vid&123)") == 0);
}
