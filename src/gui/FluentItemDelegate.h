#pragma once

#include <QStyledItemDelegate>

namespace ModeFlow::Gui {

class FluentItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit FluentItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace ModeFlow::Gui