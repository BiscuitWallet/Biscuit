// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_APPEARANCE_H
#define BISCUIT_APPEARANCE_H

#include <QIcon>

// Light or dark appearance, with the platform's own dark mode (not a
// stylesheet). Stored in Config::appearance: "system" (default), "light" or
// "dark".
namespace Appearance {
    void apply();                 // the stored choice
    bool isDark();                // what is shown now
    void toggle();                // light <-> dark, stored
    QIcon toggleIcon();           // sun in dark mode, moon in light mode
}

#endif // BISCUIT_APPEARANCE_H
