// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_ELECTRUMSERVERS_H
#define BISCUIT_ELECTRUMSERVERS_H

#include <QList>
#include <QPair>
#include <QString>

#include "CoinParams.h"

namespace biscuit::coins {

// Built-in public Electrum servers (TLS), shipped with each release: no remote
// configuration. Checked with tools/electrumprobe on 2026-09-24 (the network
// is verified from the genesis block hash). Users can set their own server.
QList<QPair<QString, quint16>> defaultElectrumServers(const CoinParams &params);

}

#endif // BISCUIT_ELECTRUMSERVERS_H
