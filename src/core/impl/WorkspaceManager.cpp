#include "WorkspaceManager.h"

#include "AudioDeviceManager.h"
#include "ConfigManager.h"
#include "DeviceUtils.h"
#include "DisplayManager.h"
#include "FontAwesome.h"
#include "WorkspaceModel.h"

namespace ModeFlow::Core {

using namespace Qt::StringLiterals;

namespace {
struct ProfileIconRule {
    QString symbol;
    QStringList keywords;
};

const QVector<ProfileIconRule>& profileIconRules() {
    static const QVector<ProfileIconRule> rules = {
        {Gui::FontAwesome::Tv, {u"tv"_s, u"movie"_s, u"video"_s, u"cinema"_s, u"телевиз"_s, u"кино"_s, u"видео"_s}},
        {Gui::FontAwesome::Gamepad, {u"game"_s, u"play"_s, u"игра"_s, u"гейм"_s, u"gaming"_s}},
        {Gui::FontAwesome::Briefcase, {u"work"_s, u"office"_s, u"job"_s, u"рабочее"_s, u"офис"_s}},
        {Gui::FontAwesome::House, {u"home"_s, u"house"_s, u"дом"_s, u"домаш"_s}},
        {Gui::FontAwesome::Music, {u"music"_s, u"audio"_s, u"sound"_s, u"музык"_s, u"аудио"_s, u"звук"_s}},
        {Gui::FontAwesome::Laptop, {u"laptop"_s, u"notebook"_s, u"ноут"_s, u"лаптоп"_s}},
        {Gui::FontAwesome::Monitor, {u"monitor"_s, u"screen"_s, u"display"_s, u"монитор"_s, u"экран"_s, u"дисплей"_s}},
        {Gui::FontAwesome::Desktop, {u"desktop"_s, u"pc"_s, u"рабочий стол"_s, u"пк"_s, u"компьютер"_s}},
    };
    return rules;
}
} // namespace

WorkspaceManager::WorkspaceManager(ConfigManager* cm, Services::DisplayManager* dm, Services::AudioDeviceManager* am)
    : m_config(cm), m_display(dm), m_audio(am) {
    m_model = std::make_unique<WorkspaceModel>(nullptr);
    m_model->setActiveIdGetter([cm]() { return cm->lastActiveProfileId(); });
    m_model->setConfigs(m_config->getWorkspaces());
}

WorkspaceManager::~WorkspaceManager() = default;

QAbstractItemModel* WorkspaceManager::model() const {
    return m_model.get();
}

void WorkspaceManager::addConfig(const WorkspaceConfig& cfg) {
    m_model->addConfig(cfg);
}

void WorkspaceManager::removeConfig(int row) {
    m_model->removeConfig(row);
}

void WorkspaceManager::updateConfig(int row, const WorkspaceConfig& cfg) {
    m_model->updateConfig(row, cfg);
}

WorkspaceConfig WorkspaceManager::captureCurrentHardwareState() const {
    WorkspaceConfig cfg;
    cfg.displayId = m_display->getCurrentDisplayKey();
    cfg.audioId = m_audio->getDefaultOutputDeviceId();
    return cfg;
}

QList<WorkspaceConfig> WorkspaceManager::configs() const {
    return m_model->configs();
}

bool WorkspaceManager::saveWorkspaces() {
    m_config->setWorkspaces(m_model->configs());
    return m_config->saveConfig();
}

void WorkspaceManager::setSelectedRow(int row) {
    const auto profiles = configs();
    if (row >= 0 && row < profiles.size())
        m_config->setSelectedProfileId(profiles[row].id);
    else
        m_config->setSelectedProfileId(QString());
}

int WorkspaceManager::selectedRow() const {
    const int selectedRow = m_model->rowOfId(m_config->selectedProfileId());
    return selectedRow != -1 ? selectedRow : m_model->rowOfId(m_config->lastActiveProfileId());
}

int WorkspaceManager::activeRow() const {
    return m_model->rowOfId(m_config->lastActiveProfileId());
}

QList<DeviceEntry> WorkspaceManager::getAvailableDisplays() const {
    QList<DeviceEntry> res;
    for (const auto& m : m_display->getPhysicalMonitors()) {
        // The cache already dropped targets that are not attached, so every entry here can be switched
        // to. Being inactive only means the display is not part of the current desktop configuration.
        res.append({m.key, m.friendlyName, /*isConnected=*/true, m.isActive});
    }
    return Utils::DeviceUtils::sortAndGroupDevices(res);
}

QList<DeviceEntry> WorkspaceManager::getAvailableAudioOutputs() const {
    QList<DeviceEntry> res;
    for (const auto& d : m_audio->getOutputDevices())
        res.append({d.id, d.displayName, d.isConnected, d.isDefault});
    return Utils::DeviceUtils::sortAndGroupDevices(res);
}

QString WorkspaceManager::generateDefaultName() {
    const auto profiles = configs();

    auto isTaken = [&](const QString& name) {
        return std::any_of(profiles.begin(), profiles.end(),
                           [&](const auto& cfg) { return cfg.name.compare(name, Qt::CaseInsensitive) == 0; });
    };

    QString nameDesktop = QObject::tr("Desktop");
    if (!isTaken(nameDesktop)) {
        return nameDesktop;
    }

    QString nameTV = QObject::tr("TV");
    if (!isTaken(nameTV)) {
        return nameTV;
    }

    QString nameBase = QObject::tr("Workspace");
    if (!isTaken(nameBase)) {
        return nameBase;
    }

    for (int i = 1; i < 100; ++i) {
        QString candidate = u"%1 %2"_s.arg(nameBase, QString::number(i));
        if (!isTaken(candidate)) {
            return candidate;
        }
    }

    return nameBase;
}

void WorkspaceManager::createDefaultProfile() {
    WorkspaceConfig cfg;
    cfg.name = generateDefaultName();
    cfg.id = QUuid::createUuid().toString();
    cfg.iconSymbol = suggestedProfileIconSymbol(cfg.name);

    addConfig(cfg);
    saveWorkspaces();
}

void WorkspaceManager::duplicateProfile(int row) {
    const auto profiles = configs();
    if (row < 0 || row >= profiles.size()) {
        return;
    }

    WorkspaceConfig duplicate = profiles[row];
    duplicate.id = QUuid::createUuid().toString();
    duplicate.name = QObject::tr("%1 (Copy)").arg(profiles[row].name);
    duplicate.hotkey = QKeySequence();

    addConfig(duplicate);
    saveWorkspaces();
}

QString WorkspaceManager::suggestedProfileIconSymbol(const QString& profileName) const {
    const QString normalized = profileName.trimmed().toLower();
    for (const auto& rule : profileIconRules()) {
        for (const auto& keyword : rule.keywords) {
            if (normalized.contains(keyword)) {
                return rule.symbol;
            }
        }
    }
    return Gui::FontAwesome::defaultProfileIconSymbol();
}

} // namespace ModeFlow::Core
