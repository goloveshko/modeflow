#include "FluentItemDelegate.h"

#include <QPainter>

#include "FontAwesome.h"
#include "StyleBridge.h"
#include "WorkspaceModel.h"

namespace ModeFlow::Gui {

FluentItemDelegate::FluentItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void FluentItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    const auto& bridge = StyleBridge::instance();
    const QRect rect = option.rect.adjusted(4, 2, -4, -2);
    const bool isSelected = option.state & QStyle::State_Selected;
    const bool isHovered = option.state & QStyle::State_MouseOver;

    const bool isActive = index.data(Core::WorkspaceModel::ActiveRole).toBool();

    // Draw rounded selection and hover backgrounds
    if (isSelected) {
        painter->setBrush(bridge.sidebarItemSelectedBg());
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(rect, 6, 6);

        // Active selection indicator bar on left edge
        painter->setBrush(bridge.sidebarAccent());
        const int barPadding = 10;
        painter->drawRoundedRect(QRect(rect.left(), rect.top() + barPadding, 3, rect.height() - 2 * barPadding), 1.5,
                                 1.5);
    } else if (isHovered) {
        painter->setBrush(bridge.sidebarItemHoverBg());
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(rect, 6, 6);
    }

    // Resolve and draw Profile Icon (20px box, 8px from left edge)
    const QRect iconRect(rect.left() + 8, rect.top(), 20, rect.height());
    const QVariant decoration = index.data(Qt::DecorationRole);

    QIcon itemIcon;
    if (decoration.typeId() == QMetaType::QIcon) {
        itemIcon = decoration.value<QIcon>();
    } else if (decoration.typeId() == QMetaType::QPixmap) {
        itemIcon = QIcon(decoration.value<QPixmap>());
    } else {
        QString symbol = decoration.toString();
        if (symbol.isEmpty()) {
            symbol = FontAwesome::defaultProfileIconSymbol();
        }

        // Tint profile icon with active/selected accent color via cached FontAwesome renderer
        const QColor iconColor =
            isSelected ? bridge.sidebarTextSelected() : (isActive ? bridge.sidebarAccent() : bridge.iconNormal());
        itemIcon = FontAwesome::icon(symbol, iconColor, 16);
    }

    itemIcon.paint(painter, iconRect, Qt::AlignCenter);

    // Draw Profile Name Text (8px gap from icon, full width right padding)
    const QString text = index.data(Qt::DisplayRole).toString();
    painter->setFont(option.font);

    QFont textFont = option.font;
    if (isSelected || isActive) {
        textFont.setWeight(QFont::DemiBold);
        painter->setFont(textFont);
    }

    const QColor textColor =
        isSelected ? bridge.sidebarTextSelected() : (isActive ? bridge.sidebarAccent() : bridge.sidebarTextNormal());
    painter->setPen(textColor);

    const int textLeft = iconRect.right() + 8;
    const int textRight = rect.right() - 10;
    const QRect textRect(textLeft, rect.top(), textRight - textLeft, rect.height());

    const QFontMetrics fm(painter->font());
    const QString elidedText = fm.elidedText(text, Qt::ElideMiddle, textRect.width());

    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedText);

    painter->restore();
}

QSize FluentItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const QSize baseSize = QStyledItemDelegate::sizeHint(option, index);
    return QSize(baseSize.width(), qMax(baseSize.height() + 4, 38));
}

} // namespace ModeFlow::Gui