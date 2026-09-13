#include "UpdateDialog.h"

#include "ui_UpdateDialog.h"

#include <QDesktopServices>

#include "IStyleManager.h"

namespace ModeFlow::Gui {

UpdateDialog::UpdateDialog(Core::IStyleManager* sm, const QString& currentVersion, const QString& latestVersion,
                           const QString& changelog, const QUrl& downloadUrl, QWidget* parent)
    : BaseDialog(sm, parent), ui(std::make_unique<Ui::UpdateDialog>()) {
    ui->setupUi(this);

    ui->labelCurrentVersion->setText(tr("Installed: %1").arg(currentVersion));
    ui->labelNewVersion->setText(tr("Latest: %1").arg(latestVersion));

    if (!changelog.isEmpty()) {
        ui->changelogBrowser->setMarkdown(changelog);
    } else {
        ui->changelogBrowser->setPlainText(tr("No changelog available."));
    }

    connect(ui->downloadBtn, &QPushButton::clicked, this, [this, downloadUrl]() {
        QDesktopServices::openUrl(downloadUrl);
        done(DownloadResult);
    });

    connect(ui->skipBtn, &QPushButton::clicked, this, [this]() { done(SkipVersionResult); });

    connect(ui->closeBtn, &QPushButton::clicked, this, [this]() { done(CloseResult); });
}

UpdateDialog::~UpdateDialog() = default;

} // namespace ModeFlow::Gui
