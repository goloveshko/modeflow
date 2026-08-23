#include "FontAwesome.h"

#include <QApplication>
#include <QDirIterator>
#include <QFontDatabase>
#include <QPainter>
#include <QStringBuilder>

#include "Logging.h"
#include "StyleBridge.h"

using namespace Qt::StringLiterals;

namespace ModeFlow::Gui {

namespace {
constexpr int DefaultIconSizePx = 16;
constexpr int DevicePixelRatioPrecision = 100;

QHash<QString, QIcon>& iconCache() {
    static QHash<QString, QIcon> cache;
    return cache;
}

qreal currentDevicePixelRatio() {
    return qApp ? qApp->devicePixelRatio() : 1.0;
}

int devicePixelRatioBucket(qreal devicePixelRatio) {
    return qRound(devicePixelRatio * DevicePixelRatioPrecision);
}

QString iconCacheKey(const QString& symbol, int size, quint64 themeRevision, int dprBucket, const QString& variant) {
    return symbol % u"_"_sv % QString::number(size) % u"_"_sv % QString::number(themeRevision) % u"_"_sv %
           QString::number(dprBucket) % u"_"_sv % variant;
}

QPixmap renderGlyphPixmap(const QString& symbol, const QString& fontFamily, const QColor& color, int targetSize,
                          qreal dpr) {
    const int physicalSize = qMax(1, qRound(targetSize * dpr));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(color);

    QFont font(fontFamily);
    font.setPixelSize(physicalSize);
    painter.setFont(font);

    painter.drawText(pixmap.rect(), Qt::AlignCenter, symbol);
    painter.end();
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}
} // namespace

QString FontAwesome::s_fontFamily = u""_s;
quint64 FontAwesome::s_themeRevision = 0;

void FontAwesome::ensureFontLoaded() {
    if (!s_fontFamily.isEmpty() && s_fontFamily != u"sans-serif"_s)
        return;

    // Fast direct lookup
    int fontId = QFontDatabase::addApplicationFont(u":/fonts/Font Awesome 7 Free-Solid-900.otf"_s);
    if (fontId == -1) {
        fontId = QFontDatabase::addApplicationFont(u":/fonts/fontawesome.otf"_s);
    }

    // Dynamic self-healing fallback: scan all embedded fonts in :/fonts or :/
    if (fontId == -1) {
        QDirIterator it(u":/fonts"_s, QStringList{u"*.otf"_s, u"*.ttf"_s}, QDir::Files, QDirIterator::Subdirectories);
        QString fallbackPath;
        QString bestMatchPath;

        while (it.hasNext()) {
            const QString path = it.next();
            if (path.contains(u"awesome"_s, Qt::CaseInsensitive)) {
                bestMatchPath = path;
                break;
            }
            if (fallbackPath.isEmpty()) {
                fallbackPath = path;
            }
        }

        const QString targetPath = bestMatchPath.isEmpty() ? fallbackPath : bestMatchPath;
        if (!targetPath.isEmpty()) {
            qCDebug(lcGui) << "Auto-discovered embedded font in resources:" << targetPath;
            fontId = QFontDatabase::addApplicationFont(targetPath);
        }
    }

    // Register font family from loaded font ID
    if (fontId != -1) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            s_fontFamily = families.at(0);
            qCDebug(lcGui) << "Successfully registered FontAwesome family:" << s_fontFamily;
            return;
        }
    }

    qCCritical(lcGui) << "CRITICAL: No valid icon font found in embedded resources! Falling back to sans-serif.";
    s_fontFamily = u"sans-serif"_s;
}

QString FontAwesome::fontFamily() {
    ensureFontLoaded();
    return s_fontFamily;
}

QIcon FontAwesome::icon(const QString& symbol, int size) {
    return icon(symbol, IconRole::Normal, size);
}

QIcon FontAwesome::icon(const QString& symbol, IconRole role, int size) {
    const auto& bridge = StyleBridge::instance();

    QColor normalColor = bridge.iconNormal();
    QColor activeColor = bridge.iconActive();
    const QColor disabledColor = bridge.iconDisabled();

    if (role == IconRole::Badge) {
        normalColor = bridge.iconBadge();
        activeColor = bridge.iconBadge();
    } else if (role == IconRole::Active) {
        normalColor = bridge.iconActive();
    } else if (role == IconRole::Disabled) {
        normalColor = bridge.iconDisabled();
    }

    return renderIcon(symbol, normalColor, activeColor, disabledColor, size, QString::number(static_cast<int>(role)));
}

QIcon FontAwesome::icon(const QString& symbol, const QColor& customColor, int size) {
    const auto& bridge = StyleBridge::instance();
    return renderIcon(symbol, customColor, customColor, bridge.iconDisabled(), size, customColor.name(QColor::HexArgb));
}

QIcon FontAwesome::renderIcon(const QString& symbol, const QColor& normalColor, const QColor& activeColor,
                              const QColor& disabledColor, int size, const QString& cacheVariant) {
    ensureFontLoaded();

    const int targetSize = size > 0 ? size : DefaultIconSizePx;
    const qreal dpr = currentDevicePixelRatio();
    const int dprBucket = devicePixelRatioBucket(dpr);

    auto& cache = iconCache();
    const QString cacheKey = iconCacheKey(symbol, targetSize, s_themeRevision, dprBucket, cacheVariant);
    auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd()) {
        return it.value();
    }

    QIcon renderedIcon;
    renderedIcon.addPixmap(renderGlyphPixmap(symbol, s_fontFamily, normalColor, targetSize, dpr), QIcon::Normal,
                           QIcon::Off);
    renderedIcon.addPixmap(renderGlyphPixmap(symbol, s_fontFamily, activeColor, targetSize, dpr), QIcon::Active,
                           QIcon::Off);
    renderedIcon.addPixmap(renderGlyphPixmap(symbol, s_fontFamily, disabledColor, targetSize, dpr), QIcon::Disabled,
                           QIcon::Off);

    cache.insert(cacheKey, renderedIcon);
    return renderedIcon;
}

void FontAwesome::invalidateCache() {
    ++s_themeRevision;
    iconCache().clear();
}

QList<FontAwesome::IconDefinition> FontAwesome::profileIconDefinitions() {
    return {
        {u"desktop"_s, QObject::tr("Desktop"), Desktop},
        {u"monitor"_s, QObject::tr("Monitor"), Monitor},
        {u"tv"_s, QObject::tr("TV"), Tv},
        {u"laptop"_s, QObject::tr("Laptop"), Laptop},
        {u"audio"_s, QObject::tr("Audio"), Audio},
        {u"music"_s, QObject::tr("Music"), Music},
        {u"video"_s, QObject::tr("Video"), Video},
        {u"gamepad"_s, QObject::tr("Gaming"), Gamepad},
        {u"briefcase"_s, QObject::tr("Work"), Briefcase},
        {u"house"_s, QObject::tr("Home"), House},
    };
}

QString FontAwesome::defaultProfileIconSymbol() {
    return Desktop;
}

} // namespace ModeFlow::Gui
