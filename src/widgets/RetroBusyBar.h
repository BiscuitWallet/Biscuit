// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_RETROBUSYBAR_H
#define BISCUIT_RETROBUSYBAR_H

#include <QTimer>
#include <QWidget>

// A small "busy" bar in the style of 1990s installers: a sunken frame with a
// group of square blocks going back and forth. Empty when idle.
class RetroBusyBar : public QWidget
{
    Q_OBJECT

public:
    explicit RetroBusyBar(QWidget *parent = nullptr);
    void setBusy(bool busy);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer m_timer;
    int m_offset = 0;
    int m_direction = 1;
    bool m_busy = false;
};

#endif // BISCUIT_RETROBUSYBAR_H
