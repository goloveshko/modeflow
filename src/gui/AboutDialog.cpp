#include "AboutDialog.h"

#include "ui_AboutDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>

#include "Logging.h"
#include "VersionInfo.h"

namespace ModeFlow::Gui {

using namespace Qt::StringLiterals;

AboutDialog::AboutDialog(Core::IStyleManager* sm, bool updateAvailable, const QString& latestVersion, QWidget* parent)
    : BaseDialog(sm, parent), ui(std::make_unique<Ui::AboutDialog>()), m_updateAvailable(updateAvailable),
      m_latestVersion(latestVersion) {
    ui->setupUi(this);

    QString version = QCoreApplication::applicationVersion();
    ui->labelVersion->setText(tr("Version %1").arg(version));

    connect(ui->btnGithub, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl(Info::GithubUrl)); });

    connect(ui->btnLicenses, &QPushButton::clicked, this, &AboutDialog::openLicense);

    connect(ui->btnNewVersion, &QPushButton::clicked, this, [this]() { done(2); });

    connect(ui->btnClose, &QPushButton::clicked, this, &AboutDialog::accept);
}

AboutDialog::~AboutDialog() = default;

void AboutDialog::openLicense() {
    const QString localPath = QDir::toNativeSeparators(QDir(qApp->applicationDirPath()).filePath(u"LICENSE.txt"_s));
    bool success = false;

    if (QFile::exists(localPath)) {
        success = QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    }

    if (!success) {
        qCWarning(lcGui) << "Could not open local license, falling back to web URL";
        const QUrl webUrl(Info::LicenseUrl);
        QDesktopServices::openUrl(webUrl);
    }
}

void AboutDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    BaseDialog::changeEvent(event);
}

void AboutDialog::showEvent(QShowEvent* event) {
    BaseDialog::showEvent(event);

    updateNewVersionButton();
}

void AboutDialog::updateNewVersionButton() {
    if (m_updateAvailable) {
        ui->btnNewVersion->setText(tr("New version available: v%1").arg(m_latestVersion));
        ui->btnNewVersion->setVisible(true);
    } else {
        ui->btnNewVersion->setVisible(false);
    }
}

} // namespace ModeFlow::Gui