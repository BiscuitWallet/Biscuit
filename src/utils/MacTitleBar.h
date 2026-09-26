// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_MACTITLEBAR_H
#define BISCUIT_MACTITLEBAR_H

#include <QtGlobal>

class QColor;
class QWidget;

// macOS: the title bar (with the red / yellow / green buttons) takes the
// window colour instead of the system's white or black. No-op elsewhere.
namespace MacTitleBar {
#if defined(Q_OS_MACOS)
    void setColor(QWidget *window, const QColor &color);
#else
    inline void setColor(QWidget *, const QColor &) {}
#endif
}

#endif // BISCUIT_MACTITLEBAR_H
