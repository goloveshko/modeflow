#include "DeviceUtils.h"

#include <algorithm>

namespace ModeFlow::Utils {

QList<Core::DeviceEntry> DeviceUtils::sortAndGroupDevices(const QList<Core::DeviceEntry>& source) {
    QList<Core::DeviceEntry> connected;
    QList<Core::DeviceEntry> disconnected;

    for (const auto& dev : source) {
        if (dev.isConnected) {
            connected.append(dev);
        } else {
            disconnected.append(dev);
        }
    }

    auto sortByName = [](const Core::DeviceEntry& a, const Core::DeviceEntry& b) {
        if (a.isActive != b.isActive) {
            return a.isActive;
        }
        return a.name.localeAwareCompare(b.name) < 0;
    };
    std::sort(connected.begin(), connected.end(), sortByName);
    std::sort(disconnected.begin(), disconnected.end(), sortByName);

    QList<Core::DeviceEntry> result = connected;
    result.append(disconnected);
    return result;
}

QList<Core::DeviceEntry> DeviceUtils::selectableDevices(const QList<Core::DeviceEntry>& devices) {
    QList<Core::DeviceEntry> result;

    // sortAndGroupDevices puts undetectable devices last, so the first one ends the offerable range.
    for (const auto& dev : devices) {
        if (!dev.isConnected) {
            break;
        }
        result.append(dev);
    }

    return result;
}

} // namespace ModeFlow::Utils