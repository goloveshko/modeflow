#pragma once

#include <QColor>
#include <QWidget>

namespace ModeFlow::Gui {

/**
 * @brief Bridge class for injecting QSS properties into C++ code.
 * Used to configure icon colors, path prefixes, and delegate styles
 * without modifying the global application palette.
 */
class StyleBridge : public QWidget {
    Q_OBJECT

    // --- Common icon settings ---
    Q_PROPERTY(QString iconprefix MEMBER m_iconPrefix)
    Q_PROPERTY(QColor iconnormal MEMBER m_iconNormal)
    Q_PROPERTY(QColor iconactive MEMBER m_iconActive)
    Q_PROPERTY(QColor icondisabled MEMBER m_iconDisabled)
    Q_PROPERTY(QColor iconbadge MEMBER m_iconBadge)

    // --- Sidebar/Navigation settings ---
    Q_PROPERTY(QColor sidebaritemhoverbg MEMBER m_sidebarItemHoverBg)
    Q_PROPERTY(QColor sidebaritemselectedbg MEMBER m_sidebarItemSelectedBg)
    Q_PROPERTY(QColor sidebartextnormal MEMBER m_sidebarTextNormal)
    Q_PROPERTY(QColor sidebartextselected MEMBER m_sidebarTextSelected)
    Q_PROPERTY(QColor sidebaraccent MEMBER m_sidebarAccent)

    // --- Log Viewer settings ---
    Q_PROPERTY(QColor logtimestamp MEMBER m_logTimestamp)
    Q_PROPERTY(QColor logcategory MEMBER m_logCategory)
    Q_PROPERTY(QColor logfunction MEMBER m_logFunction)
    Q_PROPERTY(QColor logdebug MEMBER m_logDebug)
    Q_PROPERTY(QColor loginfo MEMBER m_logInfo)
    Q_PROPERTY(QColor logwarning MEMBER m_logWarning)
    Q_PROPERTY(QColor logcritical MEMBER m_logCritical)
    Q_PROPERTY(QColor logfatal MEMBER m_logFatal)

public:
    static StyleBridge& instance();

    // Icon getters
    QString iconPrefix() const;
    QColor iconNormal() const;
    QColor iconActive() const;
    QColor iconDisabled() const;
    QColor iconBadge() const;

    // Sidebar getters
    QColor sidebarItemHoverBg() const;
    QColor sidebarItemSelectedBg() const;
    QColor sidebarTextNormal() const;
    QColor sidebarTextSelected() const;
    QColor sidebarAccent() const;

    // Log getters
    QColor logTimestamp() const;
    QColor logCategory() const;
    QColor logFunction() const;
    QColor logDebug() const;
    QColor logInfo() const;
    QColor logWarning() const;
    QColor logCritical() const;
    QColor logFatal() const;

    bool isDarkTheme() const;

    void updateStyle(const QString& qss);

private:
    explicit StyleBridge(QWidget* parent = nullptr);

    QString m_iconPrefix;
    QColor m_iconNormal;
    QColor m_iconActive;
    QColor m_iconDisabled;
    QColor m_iconBadge;

    QColor m_sidebarItemHoverBg;
    QColor m_sidebarItemSelectedBg;
    QColor m_sidebarTextNormal;
    QColor m_sidebarTextSelected;
    QColor m_sidebarAccent;

    QColor m_logTimestamp;
    QColor m_logCategory;
    QColor m_logFunction;
    QColor m_logDebug;
    QColor m_logInfo;
    QColor m_logWarning;
    QColor m_logCritical;
    QColor m_logFatal;
};

} // namespace ModeFlow::Gui