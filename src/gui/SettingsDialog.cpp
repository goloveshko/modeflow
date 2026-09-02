#include "SettingsDialog.h"

#include "ui_SettingsDialog.h"

#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>

#include "AutostartManager.h"
#include "HotkeyValidator.h"
#include "IDialogManager.h"
#include "ISettingsManager.h"
#include "IWorkspaceManager.h"
#include "LogManager.h"
#include "Logging.h"
#include "StyleUtils.h"

namespace ModeFlow::Gui {

using namespace Qt::StringLiterals;

SettingsDialog::SettingsDialog(Core::IDialogManager* dialogManager, Core::ISettingsManager* settingsManager,
                               Core::IWorkspaceManager* workspaceManager, Core::IStyleManager* styleManager,
                               QWidget* parent)
    : BaseDialog(styleManager, parent), ui(std::make_unique<Ui::SettingsDialog>()), m_dialogManager(dialogManager),
      m_settingsManager(settingsManager), m_workspaceManager(workspaceManager) {
    Q_ASSERT(m_dialogManager);
    Q_ASSERT(m_settingsManager);
    Q_ASSERT(m_workspaceManager);

    ui->setupUi(this);

    loadSettings();
    setupConnections();
    QTimer::singleShot(0, this, &SettingsDialog::initializeUiState);
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::loadSettings() {
    ui->checkAutostart->setChecked(m_settingsManager->autostartEnabled());
    ui->spinLogonDelay->setValue(m_settingsManager->autostartDelay());

    setupStartupCombo();

    setupLanguageCombo();

    setupThemeCombo();

    setupButtonBox();

    const auto originalHotkey = m_settingsManager->nextProfileHotkey();
    ui->keyEditNextProfile->setKeySequence(originalHotkey);
    ui->keyEditNextProfile->setLastAcceptedKey(originalHotkey);

    ui->checkAudioConfirmation->setChecked(m_settingsManager->audioConfirmation());
    ui->checkAutoUpdate->setChecked(m_settingsManager->autoUpdateEnabled());
    ui->checkLoggingEnabled->setChecked(m_settingsManager->loggingEnabled());

    ui->checkAskConfirmation->setChecked(m_settingsManager->askConfirmation());
}

bool SettingsDialog::saveSettings() {
    const auto previousState = m_initialState;
    const auto newState = currentFormState();

    const bool autostartNeedsUpdate = shouldUpdateAutostart(previousState, newState);

    if (newState.autoLogging != previousState.autoLogging) {
        Utils::LogManager::setup(newState.autoLogging);
    }

    if (autostartNeedsUpdate) {
        ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(false);

        m_settingsManager->requestAutostartToggleAsync(newState.autostartEnabled, newState.autostartDelay)
            .then(this, [this, previousState, newState](bool success) {
                ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);

                if (success) {
                    applySettingsState(newState);

                    if (m_settingsManager->saveSettings()) {
                        m_initialState = newState;
                        BaseDialog::accept();
                    } else {
                        m_settingsManager
                            ->requestAutostartToggleAsync(previousState.autostartEnabled, previousState.autostartDelay)
                            .then(this, [this, previousState](bool) {
                                applySettingsState(previousState);
                                ui->checkAutostart->setChecked(previousState.autostartEnabled);
                                qCWarning(lcGui) << "Unable to save settings. Changes discarded.";
                            });
                    }
                } else {
                    ui->checkAutostart->setChecked(previousState.autostartEnabled);
                    qCWarning(lcGui) << "Failed to roll back autostart state after settings save failure";
                }
            });

        return false;
    }

    applySettingsState(newState);
    if (!m_settingsManager->saveSettings()) {
        applySettingsState(previousState);
        return false;
    }

    m_initialState = newState;
    return true;
}

void SettingsDialog::setupButtonBox() {
    auto customize = [this](QDialogButtonBox::StandardButton b, const QString& text, const QString& objName) {
        if (auto* btn = ui->buttonBox->button(b)) {
            btn->setText(text);
            btn->setObjectName(objName);

            // Keep them as fully rounded Fluent push buttons (do not make them flat links)
            btn->setFlat(false);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setAutoDefault(false);

            // Make Save the primary accent button, while Cancel remains secondary
            const bool isSave = (b & QDialogButtonBox::Save) != 0;
            btn->setDefault(isSave);

            if (isSave) {
                btn->setEnabled(false);
            }

            StyleUtils::repolish(btn);
        }
    };

    // Renamed object names to standard button selectors
    customize(QDialogButtonBox::Save, tr("Save"), "btnSave");
    customize(QDialogButtonBox::Cancel, tr("Cancel"), "btnCancel");
}

void SettingsDialog::setupConnections() {
    connect(ui->checkAutostart, &QCheckBox::toggled, this, &SettingsDialog::updateUiState);
    connect(ui->spinLogonDelay, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::updateUiState);
    connect(ui->comboStartupPolicy, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::updateUiState);
    connect(ui->keyEditNextProfile, &QKeySequenceEdit::keySequenceChanged, this, &SettingsDialog::updateUiState);
    connect(ui->keyEditNextProfile, &HotkeyEdit::captureChanged, this, &SettingsDialog::hotkeyCaptureChanged);
    connect(ui->keyEditNextProfile, &HotkeyEdit::validateRequested, this, &SettingsDialog::onNextProfileHotkeyChanged);
    connect(ui->checkAudioConfirmation, &QCheckBox::toggled, this, &SettingsDialog::updateUiState);
    connect(ui->checkAutoUpdate, &QCheckBox::toggled, this, &SettingsDialog::updateUiState);
    connect(ui->checkLoggingEnabled, &QCheckBox::toggled, this, &SettingsDialog::updateUiState);
    connect(ui->checkAskConfirmation, &QCheckBox::toggled, this, &SettingsDialog::updateUiState);
    connect(ui->comboLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::updateUiState);
    connect(ui->comboTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::updateUiState);

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &BaseDialog::reject);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
}

void SettingsDialog::onNextProfileHotkeyChanged() {
    HotkeyValidator validator(m_workspaceManager, m_settingsManager, m_dialogManager);
    if (validator.validateNextProfileHotkey(ui->keyEditNextProfile, this)) {
        updateUiState();
    }
}

void SettingsDialog::initializeUiState() {
    //   Disable checkbox temporarily while we fetch the state from Task Scheduler in background
    ui->checkAutostart->setEnabled(false);

    m_settingsManager->autostartEnabledAsync().then(this, [this](bool isRegistered) {
        ui->checkAutostart->setEnabled(true);
        updateAutostartUi(isRegistered);
        captureInitialState();
        updateUiState();
    });
}

void SettingsDialog::updateUiState() {
    refreshWindowsIntegrationUi();
    refreshActionButtons();
}

void SettingsDialog::updateAutostartUi(bool active) {
    QSignalBlocker blocker(ui->checkAutostart);
    ui->checkAutostart->setChecked(active);

    if (!Services::AutostartManager::isAdmin()) {
        ui->checkAutostart->setIcon(qApp->style()->standardIcon(QStyle::SP_VistaShield));
    } else {
        ui->checkAutostart->setIcon(QIcon());
    }

    refreshWindowsIntegrationUi();
    refreshActionButtons();
}

void SettingsDialog::refreshWindowsIntegrationUi() {
    ui->spinLogonDelay->setEnabled(ui->checkAutostart->isChecked());
}

SettingsDialog::FormState SettingsDialog::currentFormState() const {
    FormState state;
    state.autostartEnabled = ui->checkAutostart->isChecked();
    state.autostartDelay = ui->spinLogonDelay->value();

    state.startupAction = ui->comboStartupPolicy->currentData(Qt::UserRole).value<Core::StartupAction>();
    state.startupProfileId = ui->comboStartupPolicy->currentData(Qt::UserRole + 1).toString();

    state.nextProfileHotkey = ui->keyEditNextProfile->keySequence();
    state.audioConfirmation = ui->checkAudioConfirmation->isChecked();
    state.autoUpdate = ui->checkAutoUpdate->isChecked();
    state.autoLogging = ui->checkLoggingEnabled->isChecked();
    state.askConfirmation = ui->checkAskConfirmation->isChecked();
    state.language = ui->comboLanguage->currentData().toString();

    state.theme = ui->comboTheme->currentData(Qt::UserRole).value<Core::Theme>();
    state.qtStyleKey = ui->comboTheme->currentData(Qt::UserRole + 1).toString();

    return state;
}

void SettingsDialog::captureInitialState() {
    m_initialState = currentFormState();
}

bool SettingsDialog::hasUnsavedChanges() const {
    return currentFormState() != m_initialState;
}

bool SettingsDialog::requiresElevation() const {
    const auto state = currentFormState();
    return shouldUpdateAutostart(m_initialState, state);
}

bool SettingsDialog::shouldUpdateAutostart(const FormState& from, const FormState& to) const {
    const bool autostartChanged = to.autostartEnabled != from.autostartEnabled;
    const bool delayChangedWhileEnabled = to.autostartEnabled && to.autostartDelay != from.autostartDelay;

    return autostartChanged || delayChangedWhileEnabled;
}

void SettingsDialog::applySettingsState(const FormState& state) {
    m_settingsManager->setAutostartDelay(state.autostartDelay);
    m_settingsManager->setStartupBehavior(state.startupAction, state.startupProfileId);
    m_settingsManager->setNextProfileHotkey(state.nextProfileHotkey);
    m_settingsManager->setAudioConfirmation(state.audioConfirmation);
    m_settingsManager->setAutoUpdateEnabled(state.autoUpdate);
    m_settingsManager->setLoggingEnabled(state.autoLogging);
    m_settingsManager->setAskConfirmation(state.askConfirmation);
    m_settingsManager->setLanguagePreference(state.language);
    m_settingsManager->setThemePreference(state.theme, state.qtStyleKey);
}

void SettingsDialog::refreshActionButtons() {
    auto* saveButton = ui->buttonBox->button(QDialogButtonBox::Save);
    if (!saveButton) {
        return;
    }

    const bool dirty = hasUnsavedChanges();
    saveButton->setEnabled(dirty);

    if (requiresElevation() && !Services::AutostartManager::isAdmin()) {
        saveButton->setIcon(qApp->style()->standardIcon(QStyle::SP_VistaShield));
    } else {
        saveButton->setIcon(QIcon());
    }

    const auto state = currentFormState();
    const bool autostartWillBeAdded = state.autostartEnabled && !m_initialState.autostartEnabled;
    const bool autostartTaskWillBeUpdated = state.autostartEnabled && m_initialState.autostartEnabled &&
                                            state.autostartDelay != m_initialState.autostartDelay;

    const auto toolTipAdmin = tr("Administrative privileges are required to apply these changes.");
    const auto toolTipAdminTip = tr("Hold Ctrl while clicking Save to enable startup logging for troubleshooting.");

    if (autostartWillBeAdded || autostartTaskWillBeUpdated) {
        saveButton->setToolTip(u"<html><head/><body><p>%1</p>"
                               "<p><span style=\" font-weight:700;\">%2</span>: "
                               "<span style=\" font-style:italic;\">%3</span></p>"
                               "</body></html>"_s.arg(toolTipAdmin, tr("Tip"), toolTipAdminTip));
    } else if (requiresElevation()) {
        saveButton->setToolTip(toolTipAdmin);
    } else {
        saveButton->setToolTip(QString());
    }
}

void SettingsDialog::setupLanguageCombo() {
    QSignalBlocker blocker(ui->comboLanguage);
    ui->comboLanguage->clear();

    auto langs = m_settingsManager->availableLanguages();
    for (const auto& lang : langs) {
        ui->comboLanguage->addItem(lang.label, lang.code);
    }

    int idx = ui->comboLanguage->findData(m_settingsManager->currentLanguage());
    ui->comboLanguage->setCurrentIndex(idx != -1 ? idx : 0);
}

void SettingsDialog::setupThemeCombo() {
    QSignalBlocker blocker(ui->comboTheme);
    ui->comboTheme->clear();

    auto themes = m_settingsManager->availableThemes();
    for (const auto& theme : themes) {
        if (theme.isSeparator)
            ui->comboTheme->insertSeparator(ui->comboTheme->count());
        else
            ui->comboTheme->addItem(theme.displayName);
        const int row = ui->comboTheme->count() - 1;
        ui->comboTheme->setItemData(row, QVariant::fromValue(theme.theme), Qt::UserRole);
        ui->comboTheme->setItemData(row, theme.styleKey, Qt::UserRole + 1);
        ui->comboTheme->setItemData(row, theme.isSeparator, Qt::UserRole + 2);
    }

    if (auto* model = qobject_cast<QStandardItemModel*>(ui->comboTheme->model())) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const bool isSeparator = ui->comboTheme->itemData(row, Qt::UserRole + 2).toBool();
            if (isSeparator) {
                if (QStandardItem* item = model->item(row)) {
                    item->setFlags(Qt::NoItemFlags);
                }
            }
        }
    }

    auto current = m_settingsManager->currentTheme();
    const QString currentQtStyleKey = m_settingsManager->currentQtStyleKey();
    int idx = -1;
    for (int i = 0; i < ui->comboTheme->count(); ++i) {
        const bool isSeparator = ui->comboTheme->itemData(i, Qt::UserRole + 2).toBool();
        if (isSeparator) {
            continue;
        }

        const auto itemTheme = ui->comboTheme->itemData(i, Qt::UserRole).value<Core::Theme>();
        const QString itemQtStyleKey = ui->comboTheme->itemData(i, Qt::UserRole + 1).toString();
        if (itemTheme == current && (current != Core::Theme::Qt || itemQtStyleKey == currentQtStyleKey)) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        for (int i = 0; i < ui->comboTheme->count(); ++i) {
            if (ui->comboTheme->itemData(i, Qt::UserRole).value<Core::Theme>() == current) {
                idx = i;
                break;
            }
        }
    }

    ui->comboTheme->setCurrentIndex(idx != -1 ? idx : 0);
}

void SettingsDialog::setupStartupCombo() {
    QSignalBlocker blocker(ui->comboStartupPolicy);
    ui->comboStartupPolicy->clear();

    ui->comboStartupPolicy->addItem(tr("Do Nothing"), QVariant::fromValue(Core::StartupAction::None));
    ui->comboStartupPolicy->addItem(tr("Last Active"), QVariant::fromValue(Core::StartupAction::LastActive));

    const auto& configs = m_workspaceManager->configs();
    for (const auto& cfg : configs) {
        ui->comboStartupPolicy->addItem(cfg.name, QVariant::fromValue(Core::StartupAction::Specific));
        int lastIdx = ui->comboStartupPolicy->count() - 1;
        ui->comboStartupPolicy->setItemData(lastIdx, cfg.id, Qt::UserRole + 1);
    }

    int targetIdx = 0;
    auto currentAction = m_settingsManager->startupAction();

    if (currentAction == Core::StartupAction::Specific) {
        QString targetId = m_settingsManager->startupProfileId();
        for (int i = 0; i < ui->comboStartupPolicy->count(); ++i) {
            if (ui->comboStartupPolicy->itemData(i, Qt::UserRole + 1).toString() == targetId) {
                targetIdx = i;
                break;
            }
        }
    } else {
        targetIdx = ui->comboStartupPolicy->findData(QVariant::fromValue(currentAction), Qt::UserRole);
    }

    ui->comboStartupPolicy->setCurrentIndex(targetIdx != -1 ? targetIdx : 0);
}

void SettingsDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    BaseDialog::changeEvent(event);
}

void SettingsDialog::closeEvent(QCloseEvent* event) {
    emit hotkeyCaptureChanged(false);

    reject();
}

void SettingsDialog::reject() {
    emit hotkeyCaptureChanged(false);
    BaseDialog::reject();
}

void SettingsDialog::accept() {
    emit hotkeyCaptureChanged(false);
    if (saveSettings()) {
        BaseDialog::accept();
    }
}

} // namespace ModeFlow::Gui
