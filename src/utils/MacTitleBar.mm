// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MacTitleBar.h"

#import <AppKit/AppKit.h>

#include <QColor>
#include <QWidget>

namespace MacTitleBar {

void setColor(QWidget *widget, const QColor &color) {
    if (!widget) {
        return;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->window()->winId());
    NSWindow *window = view.window;
    if (!window) {
        return;   // not shown yet
    }
    // The title bar becomes see-through and shows the window background.
    window.titlebarAppearsTransparent = YES;
    window.backgroundColor = [NSColor colorWithSRGBRed:color.redF() green:color.greenF() blue:color.blueF() alpha:1.0];
}

}
