// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_EVENTFILTER_H
#define FEATHER_EVENTFILTER_H

#include <QObject>

class EventFilter : public QObject
{
Q_OBJECT

public:
    explicit EventFilter(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *ev);

signals:
    void userActivity();

private:
    bool m_fontResetPending = false;
};


#endif //FEATHER_EVENTFILTER_H
