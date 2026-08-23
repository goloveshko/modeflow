#include "MainWindow.h"

#include "ui_MainWindow.h"

#include <QToolTip>

#include "Constants.h"
#include "DialogManager.h"
#include "FontAwesome.h"
#include "HotkeyValidator.h"
#include "ISettingsManager.h"
#include "IStyleManager.h"
#include "IWorkspaceManager.h"
#include "ProfileEditor.h"
#include "ProfileIconMenu.h"
#include "ProfileListView.h"
#include "ProfileTransfer.h"
#include "StyleUtils.h"

namespace ModeFlow::Gui {

using namespace Qt::StringLiterals;

namespace {
int resolvedIconExtent(const QSize& iconSize, int fallback) {
    return iconSize.width() > 0 ? iconSize.width() : fallback;
}
} // namespace

MainWindow::MainWindow(Core::IWorkspaceManager* workspaceManager, Core::ISettingsManager* settingsManager,
                       Core::IStyleManager* sm, DialogManager* dialogManager, QWidget* parent)
    : BaseDialog(sm, parent), ui(std::make_unique<Ui::MainWindow>()), m_workspaceManager(workspaceManager),
      m_settingsManager(settingsManager), m_dialogManager(dialogManager) {
    Q_ASSERT(m_workspaceManager);
    Q_ASSERT(m_settingsManager);
    Q_ASSERT(m_dialogManager);

    ui->setupUi(this);

    init();
}

MainWindow::~MainWindow() = default;

void MainWindow::init() {
    refreshVisualState();

    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(Utils::ProfileAutosaveDebounceMs);

    ui->configList->setWorkspaceManager(m_workspaceManager);
    ui->configList->setStyleManager(m_styleManager);

    ui->listApps->setDialogManager(m_dialogManager);

    m_profileIconMenu = new ProfileIconMenu(this);

    m_profileTransfer = std::make_unique<ProfileTransfer>(m_workspaceManager, m_dialogManager, this);

    ProfileEditorWidgets widgets;
    widgets.editName = ui->editName;
    widgets.keyEditSpecific = ui->keyEditSpecific;
    widgets.checkSkipInCycle = ui->checkSkipInCycle;
    widgets.comboDisplay = ui->comboDisplay;
    widgets.comboAudio = ui->comboAudio;
    widgets.listApps = ui->listApps;
    widgets.btnCapture = ui->btnCapture;
    widgets.groupGeneral = ui->groupGeneral;
    widgets.groupHardware = ui->groupHardware;
    widgets.groupAuto = ui->groupAuto;

    m_profileEditor =
        std::make_unique<ProfileEditor>(widgets, m_workspaceManager, m_settingsManager, m_profileIconMenu);

    initMoreMenu();
    setupConnections();

    restoreWindowGeometry();
    restoreSelection();
    updateUI();
}

template <typename Receiver, typename Func>
QAction* MainWindow::addMenuAction(QMenu* menu, const QString& iconSymbol, const QString& text,
                                   const Receiver* receiver, Func slot) {
    QIcon icon = iconSymbol.isEmpty() ? QIcon() : FontAwesome::icon(iconSymbol, FontAwesome::IconRole::Normal, 20);
    QAction* action = menu->addAction(icon, text, receiver, slot);
    if (!iconSymbol.isEmpty()) {
        action->setProperty("faSymbol", iconSymbol);
        action->setProperty("faRole", static_cast<int>(FontAwesome::IconRole::Normal));
    }
    return action;
}

void MainWindow::initMoreMenu() {
    if (m_moreMenu) {
        m_moreMenu->deleteLater();
    }

    m_moreMenu = new QMenu(this);
    m_moreMenu->setObjectName("moreMenu");

    addMenuAction(m_moreMenu, FontAwesome::FileImport, tr("Import Profiles"), m_profileTransfer.get(),
                  &ProfileTransfer::doImport);

    addMenuAction(m_moreMenu, FontAwesome::FileExport, tr("Export Profiles"), m_profileTransfer.get(),
                  &ProfileTransfer::doExport);

    m_moreMenu->addSeparator();

    m_checkUpdatesAction = addMenuAction(m_moreMenu, FontAwesome::CloudArrowDown, QString(), m_dialogManager,
                                         &DialogManager::forceUpdateCheck);

    addMenuAction(m_moreMenu, FontAwesome::FileLines, tr("View Log"), m_dialogManager,
                  &DialogManager::showLogViewerDialog);

    ui->btnMore->setMenu(m_moreMenu);
    ui->btnMore->setPopupMode(QToolButton::InstantPopup);

    updateMoreButtonState();
}

void MainWindow::setupConnections() {
    connect(ui->configList->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &MainWindow::on_selectionChanged);

    connect(ui->configList, &ProfileListView::createRequested, this, &MainWindow::addClicked);
    connect(ui->configList, &ProfileListView::deleteRequested, this, &MainWindow::deleteProfileByRow);
    connect(ui->configList, &ProfileListView::duplicateRequested, this, &MainWindow::duplicateProfileByRow);
    connect(ui->configList, &ProfileListView::applyRequested, this, &MainWindow::applyProfileByRow);

    connect(ui->btnCreateProfile, &QPushButton::clicked, this, &MainWindow::addClicked);

    connect(ui->btnSettings, &QToolButton::clicked, this, &MainWindow::showSettingsDialog);
    connect(ui->btnAbout, &QToolButton::clicked, this, &MainWindow::showAboutDialog);

    connect(m_profileTransfer.get(), &ProfileTransfer::exchangeCompleted, this, [this]() {
        notifySettingsChanged();
        updateUI();
        restoreSelection();
    });

    connect(m_profileEditor.get(), &ProfileEditor::profileChanged, this, &MainWindow::scheduleAutosave);
    connect(m_profileEditor.get(), &ProfileEditor::hotkeyCaptureChanged, this, &MainWindow::hotkeyCaptureChanged);
    connect(m_profileEditor.get(), &ProfileEditor::validateSpecificHotkey, this, &MainWindow::validateSpecificHotkey);

    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveCurrentProfile);
}

void MainWindow::raiseWindow() {
    if (isMinimized()) {
        showNormal();
    } else if (!isVisible()) {
        show();
    }
    raise();
    activateWindow();
}

void MainWindow::validateSpecificHotkey() {
    if (m_isUpdating)
        return;

    const int row = currentRow();
    if (row < 0)
        return;

    const auto& configs = m_workspaceManager->configs();
    const QString currentId = configs.at(row).id;

    auto* keyEdit = ui->keyEditSpecific;

    HotkeyValidator validator(m_workspaceManager, m_settingsManager, m_dialogManager);
    if (validator.validateProfileHotkey(keyEdit, currentId, this)) {
        saveCurrentToModel(row);
        scheduleAutosave();
    }
}

void MainWindow::restoreSelection() {
    int row = m_workspaceManager->selectedRow();

    if (row == -1 && m_workspaceManager->model()->rowCount() > 0) {
        row = 0;
    }

    if (row != -1) {
        ui->configList->setCurrentIndex(m_workspaceManager->model()->index(row, 0));
    }
}

void MainWindow::addClicked() {
    int row = currentRow();
    if (row != -1)
        saveCurrentToModel(row);

    m_workspaceManager->createDefaultProfile();

    int lastRow = m_workspaceManager->model()->rowCount() - 1;
    persistProfiles();
    setCurrentRowSilently(lastRow);

    captureCurrentSettings();
    updateUI();
}

void MainWindow::deleteClicked() {
    QModelIndex index = currentIndex();
    if (!index.isValid())
        return;

    int rowToDelete = index.row();

    if (!m_dialogManager->confirmAction(
            this, tr("Delete"), tr("Delete configuration '%1'?").arg(m_workspaceManager->configs()[rowToDelete].name)))
        return;

    m_isUpdating = true;
    ui->configList->setCurrentIndex(QModelIndex());
    m_isUpdating = false;

    m_workspaceManager->removeConfig(rowToDelete);
    persistProfiles();

    int rowCount = m_workspaceManager->model()->rowCount();
    if (rowCount > 0) {
        setCurrentRowSilently(std::clamp(rowToDelete, 0, rowCount - 1));
    } else {
        setCurrentRowSilently(-1);
        updateUI();
    }
}

void MainWindow::on_selectionChanged(const QModelIndex& current, const QModelIndex& previous) {
    if (m_isUpdating)
        return;

    if (previous.isValid()) {
        // High-end UX optimization: If we have pending unsaved edits (autosave timer is active),
        // stop the timer and force write the edits now before switching rows.
        // If no edits were made, we avoid redundant disk I/O and heavy hotkey re-registrations!
        if (m_autosaveTimer && m_autosaveTimer->isActive()) {
            m_autosaveTimer->stop();
            autosaveCurrentProfile();
        }
    }

    if (current.isValid()) {
        ui->settingsLayout->setEnabled(true);
        loadRowToUi(current.row());
    } else {
        ui->settingsLayout->setEnabled(false);
    }
}

void MainWindow::on_btnCapture_clicked() {
    captureCurrentSettings();
}

void MainWindow::saveCurrentToModel(int row) {
    if (row < 0 || row >= m_workspaceManager->model()->rowCount())
        return;

    Core::WorkspaceConfig cfg = m_workspaceManager->configs().at(row);
    m_profileEditor->saveProfile(cfg);

    m_workspaceManager->updateConfig(row, cfg);
}

void MainWindow::autosaveCurrentProfile() {
    const int row = currentRow();
    if (m_isUpdating || row < 0 || row >= m_workspaceManager->model()->rowCount()) {
        return;
    }

    saveCurrentToModel(row);
    persistProfiles();
}

void MainWindow::scheduleAutosave() {
    if (m_isUpdating || currentRow() < 0 || !m_autosaveTimer) {
        return;
    }

    saveCurrentToModel(currentRow());
    m_autosaveTimer->start();
}

void MainWindow::loadRowToUi(int row) {
    if (row < 0 || row >= m_workspaceManager->model()->rowCount())
        return;
    m_isUpdating = true;
    const auto& cfg = m_workspaceManager->configs().at(row);

    m_profileEditor->loadProfile(cfg);

    m_isUpdating = false;
}

void MainWindow::notifySettingsChanged() {
    emit profilesChanged();
}

bool MainWindow::persistProfiles() {
    if (!m_workspaceManager->saveWorkspaces()) {
        return false;
    }

    notifySettingsChanged();
    return true;
}

void MainWindow::setCurrentRowSilently(int row) {
    m_isUpdating = true;

    if (row >= 0 && row < m_workspaceManager->model()->rowCount()) {
        ui->configList->setCurrentIndex(m_workspaceManager->model()->index(row, 0));
        ui->settingsLayout->setEnabled(true);
        loadRowToUi(row);
    } else {
        ui->configList->setCurrentIndex(QModelIndex());
        ui->settingsLayout->setEnabled(false);
    }

    m_isUpdating = false;
}

void MainWindow::captureCurrentSettings() {
    if (currentRow() < 0)
        return;

    m_profileEditor->captureCurrentSettings();
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        QSignalBlocker blocker(this);
        ui->retranslateUi(this);
        initMoreMenu();
        loadRowToUi(currentRow());
    }
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange ||
        event->type() == QEvent::StyleChange) {
        QSignalBlocker blocker(this);
        refreshVisualState();
        update();
    }
    if (event->type() == QEvent::WindowStateChange) {
        m_settingsManager->setMainWindowMaximized(isMaximized());
    }
    BaseDialog::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    BaseDialog::showEvent(event);

    if (!m_styleManager)
        return;

    // DWM/Mica title-bar settings can be dropped after hide/show cycles.
    // Re-apply once the window is shown so the native frame is in a stable state.
    if (isVisible()) {
        m_settingsManager->setMainWindowVisible(true);

        refreshVisualState();
        ui->configList->viewport()->update();
        update();

        if (m_firstShow) {
            m_firstShow = false;
            if (m_settingsManager->mainWindowMaximized()) {
                showMaximized();
            }
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    emit hotkeyCaptureChanged(false);

    // Stop the autosave timer safely.
    // If the timer is active (meaning there are pending unsaved edits), force write them now.
    // If no edits were made, we avoid redundant disk I/O and heavy hotkey re-registrations on hide!
    if (m_autosaveTimer) {
        if (m_autosaveTimer->isActive()) {
            m_autosaveTimer->stop();
            autosaveCurrentProfile();
        } else {
            m_autosaveTimer->stop();
        }
    }

    int row = currentRow();
    m_workspaceManager->setSelectedRow(row);

    event->accept();
}

void MainWindow::hideEvent(QHideEvent* event) {
    saveWindowGeometry();
    BaseDialog::hideEvent(event);
}

void MainWindow::updateUI() {
    bool enable = m_workspaceManager->model()->rowCount() > 0;

    m_profileEditor->updateUI(enable);
}

int MainWindow::currentRow() const {
    return ui->configList->currentIndex().row();
}

QModelIndex MainWindow::currentIndex() const {
    return ui->configList->currentIndex();
}

bool MainWindow::toggleVisibility() {
    if (isVisible()) {
        emit hotkeyCaptureChanged(false);
        hide();
        return false;
    }

    raiseWindow();
    return true;
}

void MainWindow::setUpdateAvailable(bool available, const QString& version) {
    m_hasPendingUpdate = available;
    m_pendingUpdateVersion = version;

    updateMoreButtonState();
    refreshVisualState();
}

void MainWindow::updateMoreButtonState() {
    ui->btnMore->setProperty("hasUpdate", m_hasPendingUpdate);

    if (m_hasPendingUpdate) {
        ui->btnMore->setToolTip(tr("Update available: v%1 — click for details").arg(m_pendingUpdateVersion));
        if (m_checkUpdatesAction) {
            m_checkUpdatesAction->setText(tr("Update Available (v%1)...").arg(m_pendingUpdateVersion));
            m_checkUpdatesAction->setProperty("faRole", static_cast<int>(FontAwesome::IconRole::Badge));
        }
    } else {
        ui->btnMore->setToolTip(tr("More options"));
        if (m_checkUpdatesAction) {
            m_checkUpdatesAction->setText(tr("Check for Updates..."));
            m_checkUpdatesAction->setProperty("faRole", static_cast<int>(FontAwesome::IconRole::Normal));
        }
    }

    StyleUtils::repolish(ui->btnMore);
}

void MainWindow::refreshMenuIcons(QMenu* menu) {
    if (!menu)
        return;

    for (QAction* action : menu->actions()) {
        const QString symbol = action->property("faSymbol").toString();
        if (!symbol.isEmpty()) {
            const int roleInt = action->property("faRole").toInt();
            const auto role = static_cast<FontAwesome::IconRole>(roleInt);

            // Re-render icon respecting the assigned IconRole (Badge or Normal)
            action->setIcon(FontAwesome::icon(symbol, role, 20));
        }
    }
}

void MainWindow::refreshVisualState() {
    const int toolbarIconSize = resolvedIconExtent(ui->btnSettings->iconSize(), 16);

    ui->btnCreateProfile->setIcon(FontAwesome::icon(FontAwesome::Plus, toolbarIconSize));
    ui->btnSettings->setIcon(FontAwesome::icon(FontAwesome::Settings, toolbarIconSize));
    ui->btnMore->setIcon(FontAwesome::icon(FontAwesome::EllipsisVertical, toolbarIconSize));
    ui->btnAbout->setIcon(FontAwesome::icon(FontAwesome::Info, toolbarIconSize));

    // Automatically re-generate icons for all menu actions using the faSymbol property
    refreshMenuIcons(m_moreMenu);

    if (m_profileEditor) {
        m_profileEditor->refreshVisualState();
    }

    ui->configList->viewport()->update();
}

void MainWindow::showToolTipOnMoreButton(const QString& text) {
    if (!isVisible())
        return;

    int x = ui->btnMore->width() / 2;
    int y = -4;

    QPoint globalPos = ui->btnMore->mapToGlobal(QPoint(x, y));

    QToolTip::showText(globalPos, text, ui->btnMore);
}

void MainWindow::deleteProfileByRow(int row) {
    if (row < 0 || row >= m_workspaceManager->model()->rowCount())
        return;

    if (!m_dialogManager->confirmAction(this, tr("Delete"),
                                        tr("Delete configuration '%1'?").arg(m_workspaceManager->configs()[row].name)))
        return;

    m_isUpdating = true;
    ui->configList->setCurrentIndex(QModelIndex());
    m_isUpdating = false;

    m_workspaceManager->removeConfig(row);
    persistProfiles();

    int rowCount = m_workspaceManager->model()->rowCount();
    if (rowCount > 0) {
        setCurrentRowSilently(std::clamp(row, 0, rowCount - 1));
    } else {
        setCurrentRowSilently(-1);
        updateUI();
    }
}

void MainWindow::duplicateProfileByRow(int row) {
    if (row < 0 || row >= m_workspaceManager->model()->rowCount())
        return;

    saveCurrentToModel(currentRow());

    m_workspaceManager->duplicateProfile(row);

    int lastRow = m_workspaceManager->model()->rowCount() - 1;
    setCurrentRowSilently(lastRow);
}

void MainWindow::applyProfileByRow(int row) {
    if (row < 0 || row >= m_workspaceManager->model()->rowCount())
        return;

    const auto& cfg = m_workspaceManager->configs().at(row);

    emit activateProfile(cfg);
}

void MainWindow::saveWindowGeometry() {
    const bool maximized = m_settingsManager->mainWindowMaximized();
    if (!maximized) {
        m_settingsManager->setMainWindowPos(pos());
        m_settingsManager->setMainWindowSize(size());
    }
}

void MainWindow::restoreWindowGeometry() {
    const QPoint savedPos = m_settingsManager->mainWindowPos();
    const QSize savedSize = m_settingsManager->mainWindowSize();

    auto centerOnPrimary = [this]() {
        if (auto* primary = QGuiApplication::primaryScreen()) {
            const QRect screenGeom = primary->geometry();
            const int x = screenGeom.left() + (screenGeom.width() - 600) / 2;
            const int y = screenGeom.top() + (screenGeom.height() - 450) / 2;
            move(x, y);
            resize(600, 450);
        } else {
            resize(600, 450);
        }
    };

    if (!savedPos.isNull() && savedSize.isValid()) {
        // Multi-monitor safety check: verify if the saved position lies within any active monitor bounds.
        // This prevents the window from being rendered off-screen if a monitor was disconnected.
        bool posIsVisibleOnAnyMonitor = false;
        for (auto* screen : QGuiApplication::screens()) {
            if (screen->geometry().contains(savedPos)) {
                posIsVisibleOnAnyMonitor = true;
                break;
            }
        }

        if (posIsVisibleOnAnyMonitor) {
            move(savedPos);
            resize(savedSize);
        } else {
            centerOnPrimary(); // Fallback if old monitor is missing
        }
    } else {
        centerOnPrimary(); // Default size for first launch
    }
}

} // namespace ModeFlow::Gui
