#include "ProfileEditor.h"

#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>

#include "AppListWidget.h"
#include "FontAwesome.h"
#include "HotkeyEdit.h"
#include "ISettingsManager.h"
#include "IWorkspaceManager.h"
#include "ProfileIconMenu.h"

namespace ModeFlow::Gui {

ProfileEditor::ProfileEditor(const ProfileEditorWidgets& widgets, Core::IWorkspaceManager* wm,
                             Core::ISettingsManager* sm, ProfileIconMenu* iconMenu, QObject* parent)
    : QObject(parent), m_widgets(widgets), m_workspaceManager(wm), m_settingsManager(sm), m_profileIconMenu(iconMenu) {

    m_iconAction = m_widgets.editName->addAction(QIcon(), QLineEdit::LeadingPosition);
    if (m_iconAction) {
        m_iconAction->setToolTip(tr("Choose icon"));
    }

    setCurrentProfileIconSymbol(
        m_workspaceManager->suggestedProfileIconSymbol(FontAwesome::defaultProfileIconSymbol()));
    setupConnections();
}

void ProfileEditor::setupConnections() {
    connect(m_iconAction, &QAction::triggered, this, [this]() {
        if (!m_profileIconMenu)
            return;
        m_profileIconMenu->setCurrentIcon(currentProfileIconSymbol());
        const QPoint popupPos = m_widgets.editName->mapToGlobal(QPoint(0, m_widgets.editName->height()));
        m_profileIconMenu->popup(popupPos);
    });

    connect(m_profileIconMenu, &ProfileIconMenu::iconSelected, this, [this](const QString& symbol) {
        m_iconIsManual = true;
        setCurrentProfileIconSymbol(symbol);
        emit profileChanged();
    });

    connect(m_widgets.editName, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_isUpdating || m_iconIsManual)
            return;

        const QString suggestedIcon = m_workspaceManager->suggestedProfileIconSymbol(text);
        if (currentProfileIconSymbol() != suggestedIcon) {
            setCurrentProfileIconSymbol(suggestedIcon);
        }
        emit profileChanged();
    });

    connect(m_widgets.checkSkipInCycle, &QCheckBox::toggled, this, [this](bool) {
        if (!m_isUpdating)
            emit profileChanged();
    });

    connect(m_widgets.comboDisplay, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_isUpdating)
            emit profileChanged();
    });

    connect(m_widgets.comboAudio, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_isUpdating)
            emit profileChanged();
    });

    connect(m_widgets.keyEditSpecific, &HotkeyEdit::captureChanged, this, &ProfileEditor::hotkeyCaptureChanged);
    connect(m_widgets.keyEditSpecific, &HotkeyEdit::validateRequested, this, &ProfileEditor::validateSpecificHotkey);
    connect(m_widgets.listApps, &AppListWidget::appsChanged, this, &ProfileEditor::profileChanged);
    connect(m_widgets.btnCapture, &QPushButton::clicked, this, &ProfileEditor::captureCurrentSettings);
}

void ProfileEditor::loadProfile(const Core::WorkspaceConfig& cfg) {
    m_isUpdating = true;
    const QString suggestedIcon = m_workspaceManager->suggestedProfileIconSymbol(cfg.name);
    m_iconIsManual = !cfg.iconSymbol.isEmpty() && cfg.iconSymbol != suggestedIcon;

    m_widgets.editName->setText(cfg.name);
    const QString iconSymbol = cfg.iconSymbol.isEmpty() ? suggestedIcon : cfg.iconSymbol;
    setCurrentProfileIconSymbol(iconSymbol);

    m_widgets.keyEditSpecific->setKeySequence(cfg.hotkey);
    m_widgets.keyEditSpecific->setLastAcceptedKey(cfg.hotkey);
    m_widgets.checkSkipInCycle->setChecked(cfg.skipInCycle);

    m_widgets.listApps->setApps(cfg.appsToLaunch);

    updateMonitorDevices(cfg.displayId);
    updateAudioDevices(cfg.audioId);
    m_isUpdating = false;
}

void ProfileEditor::saveProfile(Core::WorkspaceConfig& cfg) {
    cfg.name = m_widgets.editName->text();
    cfg.iconSymbol = currentProfileIconSymbol();
    cfg.hotkey = m_widgets.keyEditSpecific->keySequence();
    cfg.displayId = m_widgets.comboDisplay->currentData().toString();
    cfg.audioId = m_widgets.comboAudio->currentData().toString();
    cfg.skipInCycle = m_widgets.checkSkipInCycle->isChecked();
    cfg.appsToLaunch = m_widgets.listApps->apps();
}

void ProfileEditor::updateUI(bool hasProfiles) {
    m_widgets.groupGeneral->setEnabled(hasProfiles);
    m_widgets.groupHardware->setEnabled(hasProfiles);
    m_widgets.groupAuto->setEnabled(hasProfiles);

    if (!hasProfiles) {
        m_widgets.editName->clear();
        m_iconIsManual = false;
        setCurrentProfileIconSymbol(FontAwesome::defaultProfileIconSymbol());
        m_widgets.keyEditSpecific->setKeySequence(QKeySequence());
        m_widgets.listApps->setApps({});
    }
}

void ProfileEditor::captureCurrentSettings() {
    auto currentHardware = m_workspaceManager->captureCurrentHardwareState();

    QSignalBlocker b1(m_widgets.comboDisplay);
    QSignalBlocker b2(m_widgets.comboAudio);

    int dIdx = m_widgets.comboDisplay->findData(currentHardware.displayId);
    if (dIdx != -1)
        m_widgets.comboDisplay->setCurrentIndex(dIdx);

    int aIdx = m_widgets.comboAudio->findData(currentHardware.audioId);
    if (aIdx != -1)
        m_widgets.comboAudio->setCurrentIndex(aIdx);

    emit profileChanged();
}

void ProfileEditor::updateMonitorDevices(const QString& savedDisplayId) {
    auto devices = m_workspaceManager->getAvailableDisplays();
    populateDeviceCombo(m_widgets.comboDisplay, devices, savedDisplayId);
}

void ProfileEditor::updateAudioDevices(const QString& savedAudioId) {
    auto devices = m_workspaceManager->getAvailableAudioOutputs();
    populateDeviceCombo(m_widgets.comboAudio, devices, savedAudioId);
}

void ProfileEditor::populateDeviceCombo(QComboBox* combo, const QList<Core::DeviceEntry>& devices,
                                        const QString& currentId) {
    if (!combo)
        return;
    combo->clear();
    combo->addItem(tr("— No Action —"), QString());

    if (!devices.isEmpty()) {
        combo->insertSeparator(combo->count());

        bool separatorAdded = false;
        for (const auto& device : devices) {
            if (!device.isConnected && !separatorAdded) {
                combo->insertSeparator(combo->count());
                separatorAdded = true;
            }

            combo->addItem(device.name, device.id);

            if (!device.isConnected) {
                const int lastIndex = combo->count() - 1;
                combo->setItemData(lastIndex, QBrush(Qt::gray), Qt::ForegroundRole);
            }
        }
    }

    const int idx = combo->findData(currentId);
    if (idx != -1) {
        combo->setCurrentIndex(idx);
        return;
    }

    // A profile can point at a device that is not detected right now, so keep it selectable instead of
    // silently dropping the binding: only an explicit "— No Action —" clears it.
    combo->insertSeparator(combo->count());
    combo->addItem(tr("— Saved device (not detected) —"), currentId);
    combo->setCurrentIndex(combo->count() - 1);
}

void ProfileEditor::updateProfileIconButton() {
    const QString targetSymbol = currentProfileIconSymbol();
    if (m_iconAction) {
        m_iconAction->setIcon(FontAwesome::icon(targetSymbol, 16));
    }
}

void ProfileEditor::refreshVisualState() {
    updateProfileIconButton();
}

void ProfileEditor::setCurrentProfileIconSymbol(const QString& symbol) {
    m_iconSymbol = symbol.isEmpty() ? FontAwesome::defaultProfileIconSymbol() : symbol;
    updateProfileIconButton();
}

} // namespace ModeFlow::Gui