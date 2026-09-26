// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MacTitleBar.h"

#import <AppKit/AppKit.h>

#include <QWidget>

namespace MacTitleBar {

int extendUnderTitleBar(QWidget *widget) {
    if (!widget) {
        return 0;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->window()->winId());
    NSWindow *window = view.window;
    if (!window) {
        return 0;   // not shown yet
    }
    window.titlebarAppearsTransparent = YES;
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    const CGFloat height = NSHeight(window.frame) - NSHeight(window.contentLayoutRect);
    return qMax(0, qRound(height));
}

}
