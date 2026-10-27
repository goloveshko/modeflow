#include "AudioDeviceManager.h"

#include <QMutex>
#include <QThreadPool>

#include "ComInitGuard.h"
#include "Logging.h"

// Include third-party dependency strictly inside .cpp!
#include "AudioDevices/AudioDevices.h"

namespace FE = FredEmmott::Audio;

namespace ModeFlow::Services {
using namespace Qt::StringLiterals;

struct AudioDeviceManagerPrivate {
    FE::AudioDevicePlugEventCallbackHandle plugEventHandle;
    FE::DefaultChangeCallbackHandle defaultChangeHandle;
    mutable QMutex deviceMutex;
};

AudioDeviceManager::AudioDeviceManager(QObject* parent)
    : QObject(parent), d(std::make_unique<AudioDeviceManagerPrivate>()) {

    // Callbacks are invoked from a Windows system thread.
    d->plugEventHandle =
        FE::AddAudioDevicePlugEventCallback([this](FE::AudioDevicePlugEvent event, const std::string& deviceID) {
            QString id = QString::fromStdString(deviceID);
            if (event == FE::AudioDevicePlugEvent::ADDED) {
                emit deviceAddedInternal(id);
            } else {
                emit deviceRemovedInternal(id);
            }
        });

    d->defaultChangeHandle = FE::AddDefaultAudioDeviceChangeCallback(
        [this](FE::AudioDeviceDirection direction, FE::AudioDeviceRole role, const std::string& deviceID) {
            Q_UNUSED(direction);
            if (role == FE::AudioDeviceRole::DEFAULT) {
                emit defaultDeviceChangedInternal(QString::fromStdString(deviceID));
            }
        });

    connect(this, &AudioDeviceManager::deviceAddedInternal, this, &AudioDeviceManager::deviceAdded,
            Qt::QueuedConnection);
    connect(this, &AudioDeviceManager::deviceRemovedInternal, this, &AudioDeviceManager::deviceRemoved,
            Qt::QueuedConnection);
    connect(this, &AudioDeviceManager::defaultDeviceChangedInternal, this, &AudioDeviceManager::defaultDeviceChanged,
            Qt::QueuedConnection);
}

// Explicit destructor is required in .cpp to allow std::unique_ptr to delete the forward-declared Private struct
AudioDeviceManager::~AudioDeviceManager() = default;

QList<DeviceInfo> AudioDeviceManager::getAllDevices() {
    QMutexLocker locker(&d->deviceMutex);
    QList<DeviceInfo> result;
    auto outputs = FE::GetAudioDeviceList(FE::AudioDeviceDirection::OUTPUT);
    std::string defaultOutputId =
        FE::GetDefaultAudioDeviceID(FE::AudioDeviceDirection::OUTPUT, FE::AudioDeviceRole::DEFAULT);
    for (const auto& [id, info] : outputs) {
        result.append(mapInfo(&info, defaultOutputId));
    }
    auto inputs = FE::GetAudioDeviceList(FE::AudioDeviceDirection::INPUT);
    std::string defaultInputId =
        FE::GetDefaultAudioDeviceID(FE::AudioDeviceDirection::INPUT, FE::AudioDeviceRole::DEFAULT);
    for (const auto& [id, info] : inputs) {
        result.append(mapInfo(&info, defaultInputId));
    }
    return result;
}

QList<DeviceInfo> AudioDeviceManager::getOutputDevices() {
    QMutexLocker locker(&d->deviceMutex);
    return getDevicesByDirection(static_cast<int>(FE::AudioDeviceDirection::OUTPUT));
}

QList<DeviceInfo> AudioDeviceManager::getInputDevices() {
    QMutexLocker locker(&d->deviceMutex);
    return getDevicesByDirection(static_cast<int>(FE::AudioDeviceDirection::INPUT));
}

DeviceInfo AudioDeviceManager::getDeviceById(const QString& id) {
    QMutexLocker locker(&d->deviceMutex);
    std::string stdId = id.toStdString();

    auto outputs = FE::GetAudioDeviceList(FE::AudioDeviceDirection::OUTPUT);
    if (outputs.contains(stdId)) {
        std::string defId = FE::GetDefaultAudioDeviceID(FE::AudioDeviceDirection::OUTPUT, FE::AudioDeviceRole::DEFAULT);
        return mapInfo(&outputs.at(stdId), defId);
    }

    auto inputs = FE::GetAudioDeviceList(FE::AudioDeviceDirection::INPUT);
    if (inputs.contains(stdId)) {
        std::string defId = FE::GetDefaultAudioDeviceID(FE::AudioDeviceDirection::INPUT, FE::AudioDeviceRole::DEFAULT);
        return mapInfo(&inputs.at(stdId), defId);
    }

    return {};
}

DeviceInfo AudioDeviceManager::mapInfo(const void* infoPtr, const std::string& defaultId) {
    const auto& info = *reinterpret_cast<const FE::AudioDeviceInfo*>(infoPtr);
    DeviceInfo res;
    res.id = QString::fromStdString(info.id);
    res.displayName = QString::fromStdString(info.displayName);
    res.interfaceName = QString::fromStdString(info.interfaceName);
    res.endpointName = QString::fromStdString(info.endpointName);
    res.isInput = (info.direction == FE::AudioDeviceDirection::INPUT);
    res.isConnected = (info.state == FE::AudioDeviceState::CONNECTED);
    res.isDefault = (info.id == defaultId);
    return res;
}

QList<DeviceInfo> AudioDeviceManager::getDevicesByDirection(int direction) {
    QList<DeviceInfo> result;
    auto dir = static_cast<FE::AudioDeviceDirection>(direction);
    auto devices = FE::GetAudioDeviceList(dir);
    std::string defaultId = FE::GetDefaultAudioDeviceID(dir, FE::AudioDeviceRole::DEFAULT);
    for (const auto& [id, info] : devices) {
        result.append(mapInfo(&info, defaultId));
    }
    return result;
}

QString AudioDeviceManager::getDefaultOutputDeviceId() {
    try {
        return QString::fromStdString(
            FE::GetDefaultAudioDeviceID(FE::AudioDeviceDirection::OUTPUT, FE::AudioDeviceRole::DEFAULT));
    } catch (...) {
        return {};
    }
}

QString AudioDeviceManager::getDefaultInputDeviceId() {
    try {
        return QString::fromStdString(
            FE::GetDefaultAudioDeviceID(FE::AudioDeviceDirection::INPUT, FE::AudioDeviceRole::DEFAULT));
    } catch (...) {
        return {};
    }
}

QString AudioDeviceManager::getDeviceName(const QString& id) {
    DeviceInfo info = getDeviceById(id);
    return info.displayName.isEmpty() ? id : info.displayName;
}

void AudioDeviceManager::onSetDefaultOutputDevice(const QString& id) {
    if (id.isEmpty()) {
        qCWarning(lcService) << "AudioDeviceManager::onSetDefaultOutputDevice - empty ID, ignoring";
        return;
    }

    QThreadPool::globalInstance()->start([this, id]() {
#ifdef Q_OS_WIN
        ModeFlow::Utils::ComInitGuard comGuard(ModeFlow::Utils::ComInitGuard::Apartment::MultiThreaded);
        if (!comGuard.isOk()) {
            qCWarning(lcService) << "Failed to initialize COM on background thread for output device switch";
            return;
        }
#endif
        setDefaultOutputDevice(id);
    });
}

void AudioDeviceManager::setDefaultOutputDevice(const QString& id) {
    if (id.isEmpty()) {
        qCWarning(lcService)
            << "AudioDeviceManager::setDefaultOutputDevice - Cannot set default output device - empty ID";
        return;
    }

    try {
        FE::SetDefaultAudioDeviceID(FE::AudioDeviceDirection::OUTPUT, FE::AudioDeviceRole::DEFAULT, id.toStdString());
        qCDebug(lcService) << "Successfully set default output device:" << getDeviceName(id);
    } catch (const std::exception& e) {
        qCWarning(lcService) << "Failed to set default output device:" << e.what();
        emit errorOccurred(tr("Failed to set audio output device: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AudioDeviceManager::onSetDefaultInputDevice(const QString& id) {
    if (id.isEmpty()) {
        qCWarning(lcService) << "AudioDeviceManager::onSetDefaultInputDevice - empty ID, ignoring";
        return;
    }

    QThreadPool::globalInstance()->start([this, id]() {
#ifdef Q_OS_WIN
        ModeFlow::Utils::ComInitGuard comGuard(ModeFlow::Utils::ComInitGuard::Apartment::MultiThreaded);
        if (!comGuard.isOk()) {
            qCWarning(lcService) << "Failed to initialize COM on background thread for input device switch";
            return;
        }
#endif
        setDefaultInputDevice(id);
    });
}

void AudioDeviceManager::setDefaultInputDevice(const QString& id) {
    if (id.isEmpty()) {
        qCWarning(lcService)
            << "AudioDeviceManager::setDefaultInputDevice - Cannot set default input device - empty ID";
        return;
    }

    try {
        FE::SetDefaultAudioDeviceID(FE::AudioDeviceDirection::INPUT, FE::AudioDeviceRole::DEFAULT, id.toStdString());
        qCDebug(lcService) << "Successfully set default input device:" << getDeviceName(id);
    } catch (const std::exception& e) {
        qCWarning(lcService) << "Failed to set default input device:" << e.what();
        emit errorOccurred(tr("Failed to set audio input device: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AudioDeviceManager::setDefaultCommunicationOutputDevice(const QString& id) {
    if (id.isEmpty()) {
        qCWarning(lcService) << "Cannot set communication output device - empty ID";
        return;
    }

    QThreadPool::globalInstance()->start([this, id]() {
#ifdef Q_OS_WIN
        ModeFlow::Utils::ComInitGuard comGuard(ModeFlow::Utils::ComInitGuard::Apartment::MultiThreaded);
        if (!comGuard.isOk()) {
            return;
        }
#endif
        try {
            FE::SetDefaultAudioDeviceID(FE::AudioDeviceDirection::OUTPUT, FE::AudioDeviceRole::COMMUNICATION,
                                        id.toStdString());
            qCDebug(lcService) << "Successfully set communication output device:" << getDeviceName(id);
        } catch (const std::exception& e) {
            qCWarning(lcService) << "Failed to set communication output device:" << e.what();
            emit errorOccurred(tr("Failed to set communication audio device: %1").arg(QString::fromUtf8(e.what())));
        }
    });
}

bool AudioDeviceManager::isSystemReady() const {
    try {
        auto dummy = FE::GetDefaultAudioDeviceID(FE::AudioDeviceDirection::OUTPUT, FE::AudioDeviceRole::DEFAULT);
        return !dummy.empty();
    } catch (...) {
        return false;
    }
}

} // namespace ModeFlow::Services