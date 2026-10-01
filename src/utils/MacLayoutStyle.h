// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_MACLAYOUTSTYLE_H
#define BISCUIT_MACLAYOUTSTYLE_H

#include <QProxyStyle>

class QTabWidget;

// Fixes misaligned form rows with the native macOS style:
//  - form labels are vertically centered on their field instead of pinned to
//    the top of the row;
//  - buttons, combo boxes and line edits are laid out on their visible rect,
//    not on the larger rect that includes the macOS shadow and focus ring, so
//    a "Max" button or a currency selector sits on the same line as the field
//    next to it;
//  - tab bars named "mainTabBar" (main tabs) or "centeredTabBar" (Swap) are
//    centered, like the coin selectors, instead of pinned to the left;
//  - the selected tab has exactly the window colour (Fusion lightened it);
//  - the "Pay to" field (a multi-line PayToEdit) gets the same frame as the
//    single-line fields next to it.
// Used on every platform on top of Fusion.
class MacLayoutStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    void polish(QWidget *widget) override;
    using QProxyStyle::polish;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;

    // Draws the line under the tabs of a document-mode tab widget across its
    // whole width, under the corner widget too, from the tab widget itself.
    // Left to Qt, it was missing on Windows (main tabs, Swap tabs).
    static void drawFullTabBase(QTabWidget *tabs);
};

#endif // BISCUIT_MACLAYOUTSTYLE_H
