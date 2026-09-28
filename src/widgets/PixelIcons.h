// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_PIXELICONS_H
#define BISCUIT_PIXELICONS_H

#include <QIcon>
#include <QPixmap>

// Biscuit's own pixel-art icons (original drawings, 16x16 unless noted), sharp on Retina.
namespace PixelIcons {
    QPixmap hourglass();                        // waiting
    // A desk phone in 3/4 view, 32x32. The same picture while dialing and
    // once connected (the headline says which).
    QPixmap phone(bool connected, int phase);
    QIcon computer();                           // Home tab
    QIcon network();                            // Swap tab: a globe and two arrows
    QIcon history();                            // History tab: a clock
    QIcon send();                               // Send tab: an envelope going out
    QIcon receive();                            // Receive tab: a tray, arrow in
}

#endif // BISCUIT_PIXELICONS_H
