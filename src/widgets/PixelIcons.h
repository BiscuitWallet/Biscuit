// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_PIXELICONS_H
#define BISCUIT_PIXELICONS_H

#include <QIcon>
#include <QPixmap>

// Biscuit's own 16x16 pixel-art icons (original drawings), sharp on Retina.
namespace PixelIcons {
    QPixmap hourglass();                        // waiting
    // A yellow phone on a modem. Dialing: the lights run (`phase`); connected: all lit.
    QPixmap modem(bool connected, int phase);
    QIcon computer();                           // Home tab
    QIcon network();                            // Swap tab: a globe and two arrows
    QIcon history();                            // History tab: a clock
    QIcon send();                               // Send tab: an envelope going out
    QIcon receive();                            // Receive tab: a tray, arrow in

    // Status bar and settings icons, drawn in tools/pixelart/icons16.py, by
    // name: status_connected, status_synchronizing, status_connecting,
    // status_disconnected, status_offline, lock, settings, seed, tor_on,
    // tor_off, warning, update, account, appearance, network, storage,
    // display, transactions, plugins, misc, unlock, converter, info, monero, bitcoin,
    // litecoin, sun, moon. An unknown name gives a null icon.
    QIcon icon(const char *name);
    // The same, as a pixmap of size x size logical pixels (16 or 32).
    QPixmap pixmap(const char *name, int size = 16);
}

#endif // BISCUIT_PIXELICONS_H
