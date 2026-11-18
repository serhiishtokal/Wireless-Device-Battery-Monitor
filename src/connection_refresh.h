#pragma once
#include "devices.h"

#include <algorithm>
#include <map>

constexpr unsigned kLastConnectionRetryMs = 3000;

inline bool BatteryReadingPending(const DeviceStatus& device) {
    return device.connected && !device.asleep && device.percent < 0 && device.levelText.empty();
}

inline std::wstring BluetoothConnectionKey(std::uint64_t address) {
    return L"bt-link:" + std::to_wstring(address);
}

inline std::wstring ConnectionKey(const DeviceStatus& device) {
    return device.bluetoothAddress ? BluetoothConnectionKey(device.bluetoothAddress) : device.key;
}

// Used by the poll worker. Each connection gets one immediate scan and up to two retries.
class ConnectionRefresh {
    std::map<std::wstring, std::uint64_t> connectedAt_;

public:
    void ConnectionChanged(const std::wstring& key, bool connected, std::uint64_t now) {
        if (connected) connectedAt_.emplace(key, now);
        else connectedAt_.erase(key);
    }

    unsigned NextWait(const std::vector<DeviceStatus>& devices, std::uint64_t started,
                      std::uint64_t finished, unsigned fallback) {
        unsigned wait = fallback;
        for (auto it = connectedAt_.begin(); it != connectedAt_.end();) {
            const auto elapsed = started - it->second;
            const bool ready = std::any_of(devices.begin(), devices.end(), [&](const DeviceStatus& device) {
                return device.connected && !device.asleep &&
                       ((device.percent >= 0 && device.percent <= 100) || !device.levelText.empty()) &&
                       ConnectionKey(device) == it->first;
            });
            if (ready || elapsed >= kLastConnectionRetryMs) {
                it = connectedAt_.erase(it);
                continue;
            }
            const auto due = it->second + (elapsed < 1000 ? 1000 : kLastConnectionRetryMs);
            wait = std::min(wait, due > finished ? static_cast<unsigned>(due - finished) : 0u);
            ++it;
        }
        return wait;
    }
};
