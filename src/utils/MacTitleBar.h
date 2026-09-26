// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_MACTITLEBAR_H
#define BISCUIT_MACTITLEBAR_H

#include <QtGlobal>

class QWidget;

// macOS: the title bar (with the red / yellow / green buttons) shows the
// window's own background instead of the system's white or black. The
// window content extends under a transparent title bar, so Qt paints that
// area like the rest of the window. No-op elsewhere.
namespace MacTitleBar {
#if defined(Q_OS_MACOS)
    // Returns the title bar height to keep free at the top of the content
    // (0 if the native window does not exist yet).
    int extendUnderTitleBar(QWidget *window);
#else
    inline int extendUnderTitleBar(QWidget *) { return 0; }
#endif
}

#endif // BISCUIT_MACTITLEBAR_H
