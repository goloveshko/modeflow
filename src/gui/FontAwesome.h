#pragma once

#include <QColor>
#include <QIcon>

namespace ModeFlow::Gui {

class FontAwesome {
public:
    enum class IconRole { Normal, Active, Disabled, Badge };

    struct IconDefinition {
        QString id;
        QString label;
        QString symbol;
    };

    // Icon codes (Free Solid)
    static inline const QString Settings = QString::fromUcs4(U"\uf013");
    static inline const QString Info = QString::fromUcs4(U"\uf05a");
    static inline const QString Plus = QString::fromUcs4(U"\uf067");
    static inline const QString Trash = QString::fromUcs4(U"\uf1f8");
    static inline const QString Monitor = QString::fromUcs4(U"\uf108");
    static inline const QString Audio = QString::fromUcs4(U"\uf028");
    static inline const QString ChevronRight = QString::fromUcs4(U"\uf054");

    static inline const QString FolderOpen = QString::fromUcs4(U"\uf07c");

    static inline const QString FileLines = QString::fromUcs4(U"\uf15c");
    static inline const QString RotateRight = QString::fromUcs4(U"\uf2f9");
    static inline const QString Copy = QString::fromUcs4(U"\uf0c5");
    static inline const QString FloppyDisk = QString::fromUcs4(U"\uf0c7");
    static inline const QString ArrowDown = QString::fromUcs4(U"\uf063");
    static inline const QString ArrowUp = QString::fromUcs4(U"\uf062");
    static inline const QString FileImport = QString::fromUcs4(U"\uf56f");
    static inline const QString FileExport = QString::fromUcs4(U"\uf56e");
    static inline const QString EllipsisVertical = QString::fromUcs4(U"\uf142");
    static inline const QString Terminal = QString::fromUcs4(U"\uf120");

    static inline const QString Square = QString::fromUcs4(U"\uf0c8");
    static inline const QString CheckSquare = QString::fromUcs4(U"\uf14a");
    static inline const QString Circle = QString::fromUcs4(U"\uf111");
    static inline const QString CheckCircle = QString::fromUcs4(U"\uf058");
    static inline const QString CircleCheck = CheckCircle;
    static inline const QString Desktop = QString::fromUcs4(U"\uf390");
    static inline const QString Tv = QString::fromUcs4(U"\uf26c");
    static inline const QString Laptop = QString::fromUcs4(U"\uf109");
    static inline const QString Gamepad = QString::fromUcs4(U"\uf11b");
    static inline const QString Briefcase = QString::fromUcs4(U"\uf0b1");
    static inline const QString House = QString::fromUcs4(U"\uf015");
    static inline const QString Music = QString::fromUcs4(U"\uf001");
    static inline const QString Video = QString::fromUcs4(U"\uf03d");

    // static inline const QString PowerOff = QString::fromUcs4(U"\uf011");
    static inline const QString PowerOff = QString::fromUcs4(U"\uf2f5");
    static inline const QString EyeSlash = QString::fromUcs4(U"\uf070");
    static inline const QString CloudArrowDown = QString::fromUcs4(U"\uf0ed");

    static inline const QString Play = QString::fromUcs4(U"\uf04b");
    static inline const QString Check = QString::fromUcs4(U"\uf00c");
    static inline const QString ToggleOn = QString::fromUcs4(U"\uf205");

    /**
     * @brief Creates a QIcon from a font symbol.
     * @param symbol - icon code (e.g. FontAwesome::Settings)
     * @param size - icon size in pixels. When omitted or <= 0, a single fallback-size pixmap is generated.
     */
    static QIcon icon(const QString& symbol, int size = 0);
    static QIcon icon(const QString& symbol, IconRole role, int size = 0);
    static QIcon icon(const QString& symbol, const QColor& customColor, int size = 0);

    static QList<IconDefinition> profileIconDefinitions();
    static QString defaultProfileIconSymbol();

    static QString fontFamily();

    static void invalidateCache();

private:
    static void ensureFontLoaded();
    static QIcon renderIcon(const QString& symbol, const QColor& normalColor, const QColor& activeColor,
                            const QColor& disabledColor, int size, const QString& cacheVariant);

    static QString s_fontFamily;
    static quint64 s_themeRevision;
};

} // namespace ModeFlow::Gui
