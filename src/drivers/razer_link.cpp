#ifdef _WIN32
#include "razer_link.h"
#include "../hid.h"

#include <algorithm>
#include <dbt.h>

namespace {

bool HasLinkReport(HANDLE device) {
    PHIDP_PREPARSED_DATA descriptor;
    if (!HidD_GetPreparsedData(device, &descriptor)) return false;
    HIDP_BUTTON_CAPS field{};
    USHORT count = 1;
    const auto status = HidP_GetButtonCaps(HidP_Input, &field, &count, descriptor);
    HidD_FreePreparsedData(descriptor);
    return status == HIDP_STATUS_SUCCESS && count == 1 && field.ReportID == 0x05 &&
           field.UsagePage == 0x01 && !field.IsRange && field.NotRange.Usage == 0x03;
}

}  // namespace

void RazerLinkWatch::Open(HWND window) {
    if (notification_) return;
    const auto ignored = IgnoredHidDevices();
    if (std::find(ignored.begin(), ignored.end(), std::pair<USHORT, USHORT>{
            kBasiliskReceiverVid, kBasiliskReceiverPid }) != ignored.end()) return;

    for (const auto& c : EnumHid()) {
        if (c.vid != kBasiliskReceiverVid || c.pid != kBasiliskReceiverPid || c.iface != 1 ||
            c.page() != 0x01 || c.usage() != 0x00 || c.inLen() != sizeof(report_) ||
            c.caps.NumberInputButtonCaps != 1 || c.caps.NumberInputValueCaps != 0) continue;
        HANDLE candidate = CreateFileW(c.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (candidate == INVALID_HANDLE_VALUE) continue;
        if (!HasLinkReport(candidate)) {
            CloseHandle(candidate);
            continue;
        }
        if (device_ != INVALID_HANDLE_VALUE) {
            // The battery driver groups identical receivers under one key.
            CloseHandle(candidate);
            Close();
            return;
        }
        device_ = candidate;
    }
    if (device_ == INVALID_HANDLE_VALUE) return;

    DEV_BROADCAST_HANDLE filter{};
    filter.dbch_size = sizeof(filter);
    filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    filter.dbch_handle = device_;
    notification_ = RegisterDeviceNotificationW(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    read_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!notification_ || !read_.hEvent || !Arm()) Close();
}

bool RazerLinkWatch::Arm() {
    const HANDLE event = read_.hEvent;
    read_ = {};
    read_.hEvent = event;
    ResetEvent(event);
    if (ReadFile(device_, report_, sizeof(report_), nullptr, &read_)) {
        SetEvent(event);
        return true;
    }
    pending_ = GetLastError() == ERROR_IO_PENDING;
    return pending_;
}

RazerLinkChange RazerLinkWatch::Read() {
    DWORD size = 0;
    if (!GetOverlappedResult(device_, &read_, &size, FALSE)) {
        Close();
        return RazerLinkChange::None;
    }
    pending_ = false;
    const auto change = ParseRazerLinkReport(report_, size);
    if (!Arm()) Close();
    return change;
}

void RazerLinkWatch::CloseRead() {
    if (device_ != INVALID_HANDLE_VALUE) {
        if (pending_) {
            CancelIoEx(device_, &read_);
            DWORD size;
            GetOverlappedResult(device_, &read_, &size, TRUE);
            pending_ = false;
        }
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
    if (read_.hEvent) CloseHandle(read_.hEvent);
    read_ = {};
}

void RazerLinkWatch::Close() {
    if (notification_) UnregisterDeviceNotification(notification_);
    notification_ = nullptr;
    CloseRead();
}

void RazerLinkWatch::DeviceChange(HWND window, WPARAM event, LPARAM data) {
    if (!data || !notification_) return;
    const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
    if (header->dbch_devicetype != DBT_DEVTYP_HANDLE ||
        header->dbch_size < offsetof(DEV_BROADCAST_HANDLE, dbch_eventguid)) return;
    const auto* change = reinterpret_cast<const DEV_BROADCAST_HANDLE*>(data);
    if (change->dbch_hdevnotify != notification_) return;
    if (event == DBT_DEVICEQUERYREMOVE || event == DBT_DEVICEREMOVEPENDING) CloseRead();
    if (event == DBT_DEVICEREMOVECOMPLETE || event == DBT_DEVICEQUERYREMOVEFAILED) {
        Close();
        if (event == DBT_DEVICEQUERYREMOVEFAILED) Open(window);
    }
}
#endif
