// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_PIXELICONS_H
#define BISCUIT_PIXELICONS_H

#include <QIcon>
#include <QPixmap>

// Biscuit's own 16x16 pixel-art icons (original drawings), sharp on Retina.
namespace PixelIcons {
    QPixmap hourglass();                        // waiting
    // Dialing: the LEDs light up in turn (`phase`); connected: all green.
    QPixmap modem(bool connected, int phase);
    QIcon computer();                           // Home tab
    QIcon network();                            // Swap tab
}

#endif // BISCUIT_PIXELICONS_H
