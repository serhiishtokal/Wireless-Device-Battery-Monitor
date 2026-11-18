#pragma once
#include <cstddef>
#include <cstdint>

enum class RazerLinkChange { None, Disconnected, Connected };
constexpr unsigned kBasiliskReceiverVid = 0x1532;
constexpr unsigned kBasiliskReceiverPid = 0x00AB;
constexpr std::size_t kBasiliskLinkReportSize = 16;

inline RazerLinkChange ParseRazerLinkReport(const std::uint8_t* report, std::size_t size) {
    // Basilisk V3 Pro receiver, interface 1. Other report-5 messages are unrelated to the link.
    if (size != kBasiliskLinkReportSize || report[0] != 0x05 || report[1] != 0x09 || report[3] != 0x01)
        return RazerLinkChange::None;
    if (report[2] == 0x02) return RazerLinkChange::Disconnected;
    if (report[2] == 0x03) return RazerLinkChange::Connected;
    return RazerLinkChange::None;
}

#ifdef _WIN32
#include <windows.h>

class RazerLinkWatch {
public:
    void Open(HWND window);
    void Close();
    HANDLE ReadyEvent() const { return read_.hEvent; }
    RazerLinkChange Read();
    void DeviceChange(HWND window, WPARAM event, LPARAM data);

private:
    HANDLE device_ = INVALID_HANDLE_VALUE;
    HDEVNOTIFY notification_ = nullptr;
    OVERLAPPED read_{};
    std::uint8_t report_[kBasiliskLinkReportSize]{};
    bool pending_ = false;

    bool Arm();
    void CloseRead();
};
#endif
