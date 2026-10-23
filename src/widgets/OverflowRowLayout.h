// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_OVERFLOWROWLAYOUT_H
#define BISCUIT_OVERFLOWROWLAYOUT_H

#include <QLayout>
#include <QList>

// A row that never makes the window wider than its first item: the items,
// left to right at their preferred width, and those that don't fit are not
// drawn until there is room again. For rows where the first items matter most
// (Home's price tickers).
class OverflowRowLayout : public QLayout
{
public:
    explicit OverflowRowLayout(QWidget *parent = nullptr);
    ~OverflowRowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;

    QSize sizeHint() const override;
    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override;
    void setGeometry(const QRect &rect) override;

private:
    int gap() const;

    QList<QLayoutItem *> m_items;
};

#endif // BISCUIT_OVERFLOWROWLAYOUT_H
