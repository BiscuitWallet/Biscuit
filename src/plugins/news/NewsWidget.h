// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_NEWSWIDGET_H
#define BISCUIT_NEWSWIDGET_H

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;

// List of news posts (date, title) with the selected post's summary below and
// a button to read it on biscuitwallet.com. Everything is shown as plain text.
class NewsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NewsWidget(QWidget *parent = nullptr);

private:
    void update(const QJsonArray &news);
    void showSelected();
    void openSelected();

    QTreeWidget *m_list;
    QPushButton *m_open;
    QLabel *m_empty;
};

#endif // BISCUIT_NEWSWIDGET_H
