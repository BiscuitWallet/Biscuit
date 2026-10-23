// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "OverflowRowLayout.h"

#include <QStyle>
#include <QWidget>

OverflowRowLayout::OverflowRowLayout(QWidget *parent)
    : QLayout(parent)
{
}

OverflowRowLayout::~OverflowRowLayout() {
    while (QLayoutItem *item = takeAt(0)) {
        delete item;
    }
}

void OverflowRowLayout::addItem(QLayoutItem *item) {
    m_items.append(item);
}

int OverflowRowLayout::count() const {
    return m_items.size();
}

QLayoutItem *OverflowRowLayout::itemAt(int index) const {
    return m_items.value(index);
}

QLayoutItem *OverflowRowLayout::takeAt(int index) {
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

// The style's spacing between widgets, as in a QHBoxLayout.
int OverflowRowLayout::gap() const {
    if (spacing() >= 0) {
        return spacing();
    }
    const QWidget *parent = parentWidget();
    return parent ? parent->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, parent) : 6;
}

// Every item in one row.
QSize OverflowRowLayout::sizeHint() const {
    int width = 0;
    int height = 0;
    int shown = 0;
    for (const QLayoutItem *item : m_items) {
        if (item->isEmpty()) {
            continue;
        }
        const QSize hint = item->sizeHint();
        width += hint.width();
        height = qMax(height, hint.height());
        ++shown;
    }
    width += qMax(0, shown - 1) * gap();
    const QMargins m = contentsMargins();
    return {width + m.left() + m.right(), height + m.top() + m.bottom()};
}

// The first item only: the others give way.
QSize OverflowRowLayout::minimumSize() const {
    int width = 0;
    int height = 0;
    for (const QLayoutItem *item : m_items) {
        if (item->isEmpty()) {
            continue;
        }
        if (width == 0) {
            width = item->sizeHint().width();
        }
        height = qMax(height, item->sizeHint().height());
    }
    const QMargins m = contentsMargins();
    return {width + m.left() + m.right(), height + m.top() + m.bottom()};
}

Qt::Orientations OverflowRowLayout::expandingDirections() const {
    return {};
}

void OverflowRowLayout::setGeometry(const QRect &rect) {
    QLayout::setGeometry(rect);
    const QRect area = contentsRect();
    int x = area.left();
    bool full = false;
    for (QLayoutItem *item : m_items) {
        if (item->isEmpty()) {
            continue;
        }
        const QSize hint = item->sizeHint();
        full = full || x + hint.width() - 1 > area.right();
        // An empty geometry rather than hide(): hiding would change the size
        // hints and lay the row out again.
        item->setGeometry(full ? QRect() : QRect(QPoint(x, area.top()), QSize(hint.width(), qMin(hint.height(), area.height()))));
        x += hint.width() + gap();
    }
}
