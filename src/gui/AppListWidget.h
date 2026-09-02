#pragma once

#include <QLabel>
#include <QListWidget>

#include "ConfigTypes.h"

namespace ModeFlow::Core {
class IDialogManager;
}

namespace ModeFlow::Gui {

class AppListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit AppListWidget(QWidget* parent = nullptr);
    ~AppListWidget() override;

    void setDialogManager(Core::IDialogManager* dialogManager);

    void setApps(const QList<Core::AppLaunchConfig>& apps);
    QList<Core::AppLaunchConfig> apps() const;

signals:
    void appsChanged();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refreshList();
    void openEditDialog(int editIndex);
    void addAppToSequenceDirectly(const QString& path);
    void removeApp(int index);
    void appendAppItem(int index, const Core::AppLaunchConfig& app);
    void appendAddPlaceholderItem();

private:
    Core::IDialogManager* m_dialogManager = nullptr;
    QList<Core::AppLaunchConfig> m_apps;
};

} // namespace ModeFlow::Gui