#pragma once

#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

#include "BaseDialog.h"
#include "ConfigTypes.h"

namespace ModeFlow::Core {
class IDialogManager;
class IWorkspaceManager;
class ISettingsManager;
} // namespace ModeFlow::Core

namespace Ui {
class MainWindow;
}

namespace ModeFlow::Gui {
class SettingsDialog;
class ProfileIconMenu;
class ProfileTransfer;
class ProfileEditor;

class MainWindow : public BaseDialog {
    Q_OBJECT
public:
    MainWindow(Core::IWorkspaceManager* workspaceManager, Core::ISettingsManager* settingsManager,
               Core::IStyleManager* sm, Core::IDialogManager* dialogManager, QWidget* parent = nullptr);
    ~MainWindow();

    void raiseWindow();

    void notifySettingsChanged();
    void refreshVisualState();
    void setUpdateAvailable(bool available, const QString& version);
    void showToolTipOnMoreButton(const QString& text);

    bool toggleVisibility();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;

    bool allowMinimize() const override { return true; }
    bool allowMaximize() const override { return true; }
    bool allowAutoAdjustSize() const override { return false; }

private slots:
    void addClicked();
    void on_selectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void on_btnCapture_clicked();

    void validateSpecificHotkey();

    void deleteClicked();
    void deleteProfileByRow(int row);
    void duplicateProfileByRow(int row);
    void applyProfileByRow(int row);

signals:
    void profilesChanged();
    void activateProfile(const Core::WorkspaceConfig& config);
    void showSettingsDialog();
    void showAboutDialog();
    void showUpdateDialog();
    void showLogViewer();
    void hotkeyCaptureChanged(bool active);

private:
    void init();
    void setupConnections();
    void restoreSelection();

    void saveWindowGeometry();
    void restoreWindowGeometry();

    void saveCurrentToModel(int row);
    void autosaveCurrentProfile();
    void scheduleAutosave();
    void loadRowToUi(int row);
    bool persistProfiles();
    void setCurrentRowSilently(int row);

    void captureCurrentSettings();

    void updateUI();

    int currentRow() const;
    QModelIndex currentIndex() const;

    void initMoreMenu();
    void updateMoreButtonState();
    void refreshMenuIcons(QMenu* menu);

    template <typename Receiver, typename Func>
    QAction* addMenuAction(QMenu* menu, const QString& iconSymbol, const QString& text, const Receiver* receiver,
                           Func slot);

private:
    std::unique_ptr<Ui::MainWindow> ui;
    Core::IWorkspaceManager* m_workspaceManager;
    Core::ISettingsManager* m_settingsManager;
    Core::IDialogManager* m_dialogManager;

    ProfileIconMenu* m_profileIconMenu = nullptr;
    std::unique_ptr<ProfileTransfer> m_profileTransfer;
    std::unique_ptr<ProfileEditor> m_profileEditor;
    QMenu* m_moreMenu = nullptr;
    QAction* m_checkUpdatesAction = nullptr;

    bool m_isUpdating = false;

    QTimer* m_autosaveTimer = nullptr;
    bool m_firstShow = true;

    bool m_hasPendingUpdate = false;
    QString m_pendingUpdateVersion;
};

} // namespace ModeFlow::Gui
