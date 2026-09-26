// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_MACLAYOUTSTYLE_H
#define BISCUIT_MACLAYOUTSTYLE_H

#include <QProxyStyle>

// Fixes misaligned form rows with the native macOS style:
//  - form labels are vertically centered on their field instead of pinned to
//    the top of the row;
//  - buttons, combo boxes and line edits are laid out on their visible rect,
//    not on the larger rect that includes the macOS shadow and focus ring, so
//    a "Max" button or a currency selector sits on the same line as the field
//    next to it;
//  - the main tab bar (object name "mainTabBar", flat document mode) is
//    centered, like native macOS tabs, instead of pinned to the left.
class MacLayoutStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    void polish(QWidget *widget) override;
    using QProxyStyle::polish;
};

#endif // BISCUIT_MACLAYOUTSTYLE_H
