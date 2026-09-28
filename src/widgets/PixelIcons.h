// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_PIXELICONS_H
#define BISCUIT_PIXELICONS_H

#include <QIcon>
#include <QPixmap>

// Biscuit's own 16x16 pixel-art icons (original drawings), sharp on Retina.
namespace PixelIcons {
    QPixmap hourglass();                        // waiting
    // A dial-up modem. Dialing: the lights run (`phase`); connected: all lit.
    QPixmap modem(bool connected, int phase);
    QIcon computer();                           // Home tab
    QIcon network();                            // Swap tab: a globe and two arrows
    QIcon history();                            // History tab: a clock
    QIcon send();                               // Send tab: an envelope going out
    QIcon receive();                            // Receive tab: a tray, arrow in
}

#endif // BISCUIT_PIXELICONS_H
