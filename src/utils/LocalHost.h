// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_LOCALHOST_H
#define BISCUIT_LOCALHOST_H

#include <QHostAddress>
#include <QString>

namespace Utils {
    // True only for "localhost", *.local and private/loopback IP addresses.
    // Hostnames that merely start with digits (10.example.com) are not local,
    // so they never skip the proxy.
    inline bool isLocalHost(const QString &host) {
        if (host.compare("localhost", Qt::CaseInsensitive) == 0) {
            return true;
        }

        if (host.endsWith(".local", Qt::CaseInsensitive)) { // RFC 6762
            return true;
        }

        // Only an actual IP address can be local: QHostAddress rejects hostnames,
        // so "127.example.com" goes through the proxy like any other name.
        QHostAddress address(host);
        if (address.isNull()) {
            return false;
        }

        if (address.isLoopback()) {
            return true;
        }

        bool validipv4;
        quint32 ipv4 = address.toIPv4Address(&validipv4);
        if (!validipv4) {
            return false;
        }

        return ((ipv4 & 0xff000000) == 0x0a000000) || /*       10/8 */
               ((ipv4 & 0xff000000) == 0x00000000) || /*        0/8 */
               ((ipv4 & 0xff000000) == 0x7f000000) || /*      127/8 */
               ((ipv4 & 0xffc00000) == 0x64400000) || /*  100.64/10 */
               ((ipv4 & 0xffff0000) == 0xa9fe0000) || /* 169.254/16 */
               ((ipv4 & 0xfff00000) == 0xac100000) || /*  172.16/12 */
               ((ipv4 & 0xffff0000) == 0xc0a80000);   /* 192.168/16 */
    }
}

#endif // BISCUIT_LOCALHOST_H
