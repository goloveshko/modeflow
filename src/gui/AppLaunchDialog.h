#pragma once

#include "BaseDialog.h"
#include "ConfigTypes.h"

namespace Ui {
class AppLaunchDialog;
}

namespace ModeFlow::Core {
class IDialogManager;
class IStyleManager;
} // namespace ModeFlow::Core

namespace ModeFlow::Gui {

class AppLaunchDialog : public BaseDialog {
    Q_OBJECT
public:
    explicit AppLaunchDialog(Core::IDialogManager* dialogManager, Core::IStyleManager* styleManager,
                             QWidget* parent = nullptr);
    ~AppLaunchDialog() override;

    void setAppConfig(const Core::AppLaunchConfig& config);
    Core::AppLaunchConfig appConfig() const;

private slots:
    void browseClicked();
    void validateAndAccept();

private:
    std::unique_ptr<Ui::AppLaunchDialog> ui;
    Core::IDialogManager* m_dialogManager = nullptr;
};

} // namespace ModeFlow::Gui
