#include "StyleBridge.h"

#include "StyleUtils.h"

using namespace Qt::StringLiterals;

namespace ModeFlow::Gui {

StyleBridge& StyleBridge::instance() {
    static StyleBridge inst;
    return inst;
}

StyleBridge::StyleBridge(QWidget* parent)
    : QWidget(parent), m_iconPrefix(u":/icons/light"_s), m_iconNormal(Qt::white), m_iconActive(Qt::white),
      m_iconDisabled(Qt::gray), m_iconBadge(QColor(u"#FF9800"_s)), m_sidebarItemHoverBg(QColor(255, 255, 255, 20)),
      m_sidebarItemSelectedBg(QColor(255, 255, 255, 30)), m_sidebarTextNormal(QColor(u"#AAAAAA"_s)),
      m_sidebarTextSelected(Qt::white), m_sidebarAccent(QColor(u"#60CDFF"_s)), m_logTimestamp(QColor(u"#707070"_s)),
      m_logCategory(QColor(u"#0066CC"_s)), m_logFunction(QColor(u"#7F7F00"_s)), m_logDebug(QColor(u"#666666"_s)),
      m_logInfo(QColor(u"#2E7D32"_s)), m_logWarning(QColor(u"#E65100"_s)), m_logCritical(QColor(u"#C62828"_s)),
      m_logFatal(QColor(u"#8E0000"_s)) {
    setObjectName("styleBridge");
    setAttribute(Qt::WA_DontShowOnScreen);
    hide();
}

QString StyleBridge::iconPrefix() const {
    return m_iconPrefix;
}
QColor StyleBridge::iconNormal() const {
    return m_iconNormal;
}
QColor StyleBridge::iconActive() const {
    return m_iconActive;
}
QColor StyleBridge::iconDisabled() const {
    return m_iconDisabled;
}
QColor StyleBridge::iconBadge() const {
    return m_iconBadge;
}

QColor StyleBridge::sidebarItemHoverBg() const {
    return m_sidebarItemHoverBg;
}
QColor StyleBridge::sidebarItemSelectedBg() const {
    return m_sidebarItemSelectedBg;
}
QColor StyleBridge::sidebarTextNormal() const {
    return m_sidebarTextNormal;
}
QColor StyleBridge::sidebarTextSelected() const {
    return m_sidebarTextSelected;
}
QColor StyleBridge::sidebarAccent() const {
    return m_sidebarAccent;
}

QColor StyleBridge::logTimestamp() const {
    return m_logTimestamp;
}
QColor StyleBridge::logCategory() const {
    return m_logCategory;
}
QColor StyleBridge::logFunction() const {
    return m_logFunction;
}
QColor StyleBridge::logDebug() const {
    return m_logDebug;
}
QColor StyleBridge::logInfo() const {
    return m_logInfo;
}
QColor StyleBridge::logWarning() const {
    return m_logWarning;
}
QColor StyleBridge::logCritical() const {
    return m_logCritical;
}
QColor StyleBridge::logFatal() const {
    return m_logFatal;
}

bool StyleBridge::isDarkTheme() const {
    return m_iconPrefix.contains(u"dark"_sv);
}

void StyleBridge::updateStyle(const QString& qss) {
    setStyleSheet(qss);
    Gui::StyleUtils::repolish(this);
}

} // namespace ModeFlow::Gui