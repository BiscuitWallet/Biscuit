// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "EventFilter.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QWidget>

EventFilter::EventFilter(QObject *parent)
    : QObject(parent)
{}

bool EventFilter::eventFilter(QObject *obj, QEvent *ev) {
    if (ev->type() == QEvent::KeyPress || ev->type() == QEvent::MouseButtonRelease) {
        emit userActivity();
    }

#ifdef Q_OS_MACOS
    // Biscuit: macOS hands the system font to some windows after start-up
    // (wizard pages, a plugin tab, the status bar...), and their widgets
    // inherit it; with Fusion it draws some capitals heavier. A widget
    // without a font of its own gets the application font (main.cpp) when
    // it is first shown and whenever its font changes.
    if ((ev->type() == QEvent::Polish || ev->type() == QEvent::FontChange) && obj->isWidgetType()) {
        auto *widget = static_cast<QWidget *>(obj);
        const QFont appFont = QApplication::font();
        if (widget->font().resolveMask() == 0 && widget->font().family() != appFont.family()) {
            widget->setFont(appFont);
        }
    }

    // A theme change (light / dark, from Biscuit or the system) makes Qt
    // reload the per-class system fonts (menus, tabs, item views...). Set
    // the application font again once the change is through: this drops
    // those class fonts and hands the application font to every widget.
    if (ev->type() == QEvent::ThemeChange && !m_fontResetPending) {
        m_fontResetPending = true;
        QTimer::singleShot(0, this, [this] {
            m_fontResetPending = false;
            QApplication::setFont(QApplication::font());
        });
    }
#endif

    return QObject::eventFilter(obj, ev);
}