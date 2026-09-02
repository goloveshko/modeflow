#include "AppListWidget.h"

#include <QContextMenuEvent>
#include <QFileIconProvider>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QToolButton>

#include "FluentItemDelegate.h"
#include "FontAwesome.h"
#include "IDialogManager.h"

namespace ModeFlow::Gui {

using namespace Qt::StringLiterals;

AppListWidget::AppListWidget(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
    setItemDelegate(new FluentItemDelegate(this));
}

AppListWidget::~AppListWidget() = default;

void AppListWidget::setDialogManager(Core::IDialogManager* dialogManager) {
    m_dialogManager = dialogManager;
}

void AppListWidget::setApps(const QList<Core::AppLaunchConfig>& apps) {
    m_apps = apps;
    refreshList();
}

QList<Core::AppLaunchConfig> AppListWidget::apps() const {
    return m_apps;
}

void AppListWidget::openEditDialog(int editIndex) {
    if (!m_dialogManager)
        return;

    const Core::AppLaunchConfig* initialConfig =
        (editIndex >= 0 && editIndex < m_apps.size()) ? &m_apps[editIndex] : nullptr;

    auto result = m_dialogManager->showAppLaunchDialog(initialConfig, this);
    if (result.has_value()) {
        if (editIndex >= 0 && editIndex < m_apps.size()) {
            m_apps[editIndex] = result.value();
        } else {
            m_apps.append(result.value());
        }
        refreshList();
        emit appsChanged();
    }
}

void AppListWidget::addAppToSequenceDirectly(const QString& path) {
    Core::AppLaunchConfig cfg;
    cfg.appPath = path;
    cfg.delaySeconds = 0;
    cfg.closeOnExit = false;
    m_apps.append(cfg);

    refreshList();
    emit appsChanged();
}

void AppListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.toLocalFile().endsWith(".exe", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QListWidget::dragEnterEvent(event);
}

void AppListWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.toLocalFile().endsWith(".exe", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QListWidget::dragMoveEvent(event);
}

void AppListWidget::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.endsWith(".exe", Qt::CaseInsensitive)) {
            addAppToSequenceDirectly(path);
        }
    }
}

void AppListWidget::contextMenuEvent(QContextMenuEvent* event) {
    QListWidgetItem* item = itemAt(event->pos());

    if (item && row(item) == count() - 1) {
        event->accept();
        return;
    }

    QMenu menu(window());

    QAction* addAction = menu.addAction(FontAwesome::icon(FontAwesome::Plus, 16), tr("Add program..."));
    QAction* editAction = nullptr;
    QAction* deleteAction = nullptr;

    if (item && item->data(Qt::UserRole).isValid()) {
        editAction = menu.addAction(FontAwesome::icon(FontAwesome::Settings, 16), tr("Edit..."));

        deleteAction = menu.addAction(FontAwesome::icon(FontAwesome::Trash, 16), tr("Delete"));
    }

    QAction* selectedAction = menu.exec(event->globalPos());

    if (selectedAction == addAction) {
        openEditDialog(-1);
    } else if (selectedAction == editAction && item) {
        openEditDialog(row(item));
    } else if (selectedAction == deleteAction && item) {
        int idx = row(item);
        removeApp(idx);
    }
}

void AppListWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QListWidgetItem* item = itemAt(event->pos());
        if (item && item->data(Qt::UserRole).isValid()) {
            openEditDialog(row(item));
        } else {
            openEditDialog(-1);
        }
        return;
    }
    QListWidget::mouseDoubleClickEvent(event);
}

void AppListWidget::mousePressEvent(QMouseEvent* event) {
    QListWidgetItem* item = itemAt(event->pos());

    if (item && row(item) == count() - 1) {
        if (event->button() == Qt::LeftButton) {
            openEditDialog(-1);
        }
        event->accept();
        return;
    }

    QListWidget::mousePressEvent(event);
}

void AppListWidget::removeApp(int index) {
    if (index < 0 || index >= m_apps.size())
        return;

    const auto& app = m_apps.at(index);
    QFileInfo fi(app.appPath);
    QString appName = fi.exists() ? fi.fileName() : app.appPath;

    if (m_dialogManager) {
        bool confirmed = m_dialogManager->confirmAction(
            this, tr("Delete Program"),
            tr("Are you sure you want to remove '%1' from the startup sequence?").arg(appName));

        if (!confirmed) {
            return;
        }
    }

    m_apps.removeAt(index);
    refreshList();
    emit appsChanged();
}

void AppListWidget::refreshList() {
    clear();

    for (int i = 0; i < m_apps.size(); ++i) {
        appendAppItem(i, m_apps.at(i));
    }

    appendAddPlaceholderItem();
}

void AppListWidget::appendAppItem(int index, const Core::AppLaunchConfig& app) {
    QFileIconProvider iconProvider;
    QFileInfo fi(app.appPath);

    auto* item = new QListWidgetItem(this);
    item->setSizeHint(QSize(0, 32));

    QString entry = fi.fileName();
    if (app.delaySeconds > 0) {
        entry += tr(" (Delay: %1 sec)").arg(app.delaySeconds);
    }
    if (app.closeOnExit) {
        entry += tr(" [Auto-close]");
    }
    item->setText(entry);

    if (fi.exists()) {
        item->setIcon(iconProvider.icon(fi));
    }

    auto* rowWidget = new QWidget(this);
    rowWidget->setObjectName("appRowWidget");

    auto* layout = new QHBoxLayout(rowWidget);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->addStretch();

    auto* deleteBtn = new QToolButton(rowWidget);
    deleteBtn->setObjectName("btnDeleteAppInline");
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setIcon(FontAwesome::icon(FontAwesome::Trash, 14));
    deleteBtn->setFixedSize(24, 24);
    deleteBtn->setToolTip(tr("Remove from sequence"));
    layout->addWidget(deleteBtn);

    connect(deleteBtn, &QToolButton::clicked, this, [this, index]() { removeApp(index); });

    item->setData(Qt::UserRole, QVariant::fromValue(app));

    addItem(item);
    setItemWidget(item, rowWidget);
}

void AppListWidget::appendAddPlaceholderItem() {
    auto* item = new QListWidgetItem(this);
    item->setSizeHint(QSize(0, 32));

    item->setText(tr("Add program..."));
    item->setIcon(FontAwesome::icon(FontAwesome::Plus, 14));

    auto* addWidget = new QWidget(this);
    addWidget->setObjectName("appAddRowWidget");

    addWidget->setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(addWidget);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->addStretch();

    addItem(item);
    setItemWidget(item, addWidget);
}

void AppListWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        refreshList();
    }

    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange ||
        event->type() == QEvent::StyleChange) {

        refreshList();
        viewport()->update();
    }

    QListWidget::changeEvent(event);
}

void AppListWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        const int idx = currentRow();
        if (idx >= 0 && idx < m_apps.size()) {
            removeApp(idx);
            event->accept();
            return;
        }
    } else if (event->key() == Qt::Key_Insert) {
        openEditDialog(-1);
        event->accept();
        return;
    }

    QListWidget::keyPressEvent(event);
}

} // namespace ModeFlow::Gui