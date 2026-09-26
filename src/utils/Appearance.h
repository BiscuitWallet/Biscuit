// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_APPEARANCE_H
#define BISCUIT_APPEARANCE_H

#include <QIcon>

class QStatusBar;
class QTabWidget;

// Light or dark appearance, with the platform's own dark mode (not a
// stylesheet). Stored in Config::appearance: "system" (default), "light" or
// "dark".
namespace Appearance {
    void apply();                 // the stored choice
    bool isDark();                // what is shown now
    void toggle();                // light <-> dark, stored
    QIcon toggleIcon();           // sun in dark mode, moon in light mode

    // Flat tab bars (document mode), light grey with the selected tab lighter,
    // normal-size text; kept in step with light / dark.
    void styleTabs(QTabWidget *tabs, bool centered);
    // The status bar did not follow dark mode on macOS: colours set here.
    void styleStatusBar(QStatusBar *bar);
}

#endif // BISCUIT_APPEARANCE_H
