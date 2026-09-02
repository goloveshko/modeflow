#include "AppLaunchDialog.h"

#include "ui_AppLaunchDialog.h"

#include "FontAwesome.h"
#include "IDialogManager.h"
#include "SystemUtils.h"

namespace ModeFlow::Gui {

AppLaunchDialog::AppLaunchDialog(Core::IDialogManager* dialogManager, Core::IStyleManager* styleManager,
                                 QWidget* parent)
    : BaseDialog(styleManager, parent), ui(std::make_unique<Ui::AppLaunchDialog>()), m_dialogManager(dialogManager) {
    Q_ASSERT(m_dialogManager);
    ui->setupUi(this);

    connect(ui->btnBrowse, &QToolButton::clicked, this, &AppLaunchDialog::browseClicked);
    connect(ui->btnOk, &QPushButton::clicked, this, &AppLaunchDialog::validateAndAccept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &BaseDialog::reject);
    connect(ui->editPath, &QLineEdit::textChanged, this,
            [this](const QString&) { ui->btnOk->setEnabled(!ui->editPath->text().trimmed().isEmpty()); });

    ui->btnOk->setEnabled(false);

    ui->btnBrowse->setIcon(FontAwesome::icon(FontAwesome::FolderOpen, 16));
    ui->btnBrowse->setToolTip(tr("Browse..."));
}

AppLaunchDialog::~AppLaunchDialog() = default;

void AppLaunchDialog::setAppConfig(const Core::AppLaunchConfig& config) {
    ui->editPath->setText(config.appPath);
    ui->spinDelay->setValue(config.delaySeconds);
    ui->checkCloseOnExit->setChecked(config.closeOnExit);
}

Core::AppLaunchConfig AppLaunchDialog::appConfig() const {
    Core::AppLaunchConfig cfg;
    cfg.appPath = ui->editPath->text().trimmed();
    cfg.delaySeconds = ui->spinDelay->value();
    cfg.closeOnExit = ui->checkCloseOnExit->isChecked();
    return cfg;
}

void AppLaunchDialog::browseClicked() {
    QString filter = tr("Executable files (*.exe);;All files (*)");

    QString path = m_dialogManager->getOpenFileName(this, tr("Select Program"), ui->editPath->text(), filter);

    if (!path.isEmpty()) {
        ui->editPath->setText(path);
    }
}

void AppLaunchDialog::validateAndAccept() {
    const QString path = ui->editPath->text().trimmed();

    if (path.isEmpty()) {
        m_dialogManager->showWarning(this, tr("Validation"), tr("Please select an executable file."));
        return;
    }

    if (!Utils::SystemUtils::isValidExecutablePath(path)) {
        m_dialogManager->showWarning(this, tr("Validation"), tr("The selected file is not a valid executable."));
        return;
    }

    accept();
}

} // namespace ModeFlow::Gui
