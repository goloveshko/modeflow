#pragma once

#include <QCloseEvent>
#include <QDialog>

#include "BaseDialog.h"
#include "ConfigTypes.h"

namespace ModeFlow::Core {
class IDialogManager;
class IWorkspaceManager;
class ISettingsManager;
} // namespace ModeFlow::Core

namespace Ui {
class SettingsDialog;
}

namespace ModeFlow::Gui {

/**
 * @brief Dialog for application-wide settings.
 *
 * Handles global settings: Windows integration, startup behavior, hotkeys, language, audio.
 */
class SettingsDialog : public BaseDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Core::IDialogManager* dialogManager, Core::ISettingsManager* settingsManager,
                            Core::IWorkspaceManager* workspaceManager, Core::IStyleManager* styleManager,
                            QWidget* parent = nullptr);
    ~SettingsDialog();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

public slots:
    void updateAutostartUi(bool active);
    void reject() override;
    void accept() override;

private slots:
    void onNextProfileHotkeyChanged();
    void updateUiState();

signals:
    void settingsChanged();
    void languageChanged(const QString& locale);
    void hotkeyCaptureChanged(bool active);

private:
    struct FormState {
        bool autostartEnabled = false;
        int autostartDelay = 0;
        Core::StartupAction startupAction = Core::StartupAction::None;
        QString startupProfileId;
        QKeySequence nextProfileHotkey;
        bool audioConfirmation = false;
        bool autoUpdate = true;
        bool autoLogging = false;
        bool askConfirmation = true;
        QString language;
        Core::Theme theme = Core::Theme::Dark;
        QString qtStyleKey;

        bool operator==(const FormState&) const = default;
    };

    FormState currentFormState() const;
    void captureInitialState();
    bool hasUnsavedChanges() const;
    bool requiresElevation() const;
    bool shouldUpdateAutostart(const FormState& from, const FormState& to) const;
    void applySettingsState(const FormState& state);
    void refreshWindowsIntegrationUi();
    void refreshActionButtons();
    void loadSettings();
    bool saveSettings();
    void setupLanguageCombo();
    void setupThemeCombo();
    void setupStartupCombo();
    void setupConnections();
    void setupButtonBox();
    void initializeUiState();

private:
    std::unique_ptr<Ui::SettingsDialog> ui;
    Core::ISettingsManager* m_settingsManager;
    Core::IWorkspaceManager* m_workspaceManager;
    Core::IDialogManager* m_dialogManager = nullptr;
    FormState m_initialState;
    bool m_isValidatingHotkey = false;
};

} // namespace ModeFlow::Gui
