#pragma once

#include "ConfigTypes.h"

namespace ModeFlow::Utils {

class DeviceUtils {
public:
    static QList<Core::DeviceEntry> sortAndGroupDevices(const QList<Core::DeviceEntry>& source);
    static QList<Core::DeviceEntry> selectableDevices(const QList<Core::DeviceEntry>& devices);
};

} // namespace ModeFlow::Utils