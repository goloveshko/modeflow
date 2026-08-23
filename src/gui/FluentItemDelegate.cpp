#include "FluentItemDelegate.h"

#include <QPainter>

#include "FontAwesome.h"
#include "StyleBridge.h"
#include "WorkspaceModel.h"

namespace ModeFlow::Gui {

FluentItemDelegate::FluentItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {
    m_iconFont.setFamily(FontAwesome::fontFamily());
    m_iconFont.setPixelSize(16);
    m_iconFont.setHintingPreference(QFont::PreferNoHinting);
}

void FluentItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    const auto& bridge = StyleBridge::instance();
    const QRect rect = option.rect.adjusted(4, 2, -4, -2);
    const bool isSelected = option.state & QStyle::State_Selected;
    const bool isHovered = option.state & QStyle::State_MouseOver;

    const bool isActive = index.data(Core::WorkspaceModel::ActiveRole).toBool();

    // Draw rounded selections
    if (isSelected) {
        painter->setBrush(bridge.sidebarItemSelectedBg());
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(rect, 6, 6);

        painter->setBrush(bridge.sidebarAccent());
        const int barPadding = 10;
        painter->drawRoundedRect(QRect(rect.left(), rect.top() + barPadding, 3, rect.height() - 2 * barPadding), 1.5,
                                 1.5);
    } else if (isHovered) {
        painter->setBrush(bridge.sidebarItemHoverBg());
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(rect, 6, 6);
    }

    // Draw Shield/Plus Icon (20px box, 8px from left edge)
    const QRect iconRect(rect.left() + 8, rect.top(), 20, rect.height());
    QVariant decoration = index.data(Qt::DecorationRole);

    if (!decoration.isValid()) {
        const QString symbol = FontAwesome::defaultProfileIconSymbol();
        const QIcon itemIcon =
            FontAwesome::icon(symbol, isActive ? FontAwesome::IconRole::Active : FontAwesome::IconRole::Normal, 16);
        itemIcon.paint(painter, iconRect, Qt::AlignCenter);
    } else if (decoration.typeId() == QMetaType::QIcon || decoration.typeId() == QMetaType::QPixmap) {
        QIcon icon = decoration.value<QIcon>();
        icon.paint(painter, iconRect, Qt::AlignCenter);
    } else {
        QString symbol = decoration.toString();
        if (symbol.isEmpty()) {
            symbol = FontAwesome::defaultProfileIconSymbol();
        }

        painter->setFont(m_iconFont);

        // Visual indicator: Tint the profile icon with accent color if active
        QColor iconColor =
            isSelected ? bridge.sidebarTextSelected() : (isActive ? bridge.sidebarAccent() : bridge.iconNormal());
        painter->setPen(iconColor);
        painter->drawText(iconRect, Qt::AlignCenter, symbol);
    }

    // Draw Profile Name Text (8px gap from icon, tight right padding)
    const QString text = index.data(Qt::DisplayRole).toString();
    painter->setFont(option.font);

    QFont textFont = option.font;
    if (isSelected || isActive) {
        textFont.setWeight(QFont::DemiBold);
        painter->setFont(textFont);
    }

    QColor textColor =
        isSelected ? bridge.sidebarTextSelected() : (isActive ? bridge.sidebarAccent() : bridge.sidebarTextNormal());
    painter->setPen(textColor);

    const int textLeft = iconRect.right() + 8;
    const int textRight = rect.right() - 8;
    const QRect textRect(textLeft, rect.top(), textRight - textLeft, rect.height());

    QFontMetrics fm(painter->font());
    const QString elidedText = fm.elidedText(text, Qt::ElideMiddle, textRect.width());

    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedText);

    painter->restore();
}

QSize FluentItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const QSize baseSize = QStyledItemDelegate::sizeHint(option, index);
    const int itemHeight = qMax(baseSize.height() + 12, 38);
    return QSize(baseSize.width(), qMax(baseSize.height() + 4, 38));
}

} // namespace ModeFlow::Gui