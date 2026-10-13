// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_MACFONTS_H
#define BISCUIT_MACFONTS_H

#include <QString>

namespace MacFonts {
    // macOS: registers "Biscuit UI", Helvetica Neue whose bold is its Medium
    // face, and returns that family; empty elsewhere or if a face is missing.
    // Bold Helvetica Neue strokes are about 1.8 px wide at the UI sizes, so on
    // a non-Retina screen some fall on two dark pixels and others on one and a
    // grey one; Medium's thinner strokes look even. Every bold request (a
    // style sheet, <b>, a bold item) gets Medium through this one family.
    // Registered again when the font database is rebuilt.
    QString registerUiFamily();
}

#endif // BISCUIT_MACFONTS_H
