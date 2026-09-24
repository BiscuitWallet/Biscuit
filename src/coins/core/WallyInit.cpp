// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "WallyInit.h"

#include <mutex>

#include <QByteArray>
#include <QRandomGenerator>

#include <wally_core.h>

namespace biscuit::coins {

void ensureWallyInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        wally_init(0);
        QByteArray entropy(WALLY_SECP_RANDOMIZE_LEN, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(entropy.data()), entropy.size() / 4);
        wally_secp_randomize(reinterpret_cast<const unsigned char *>(entropy.constData()), entropy.size());
        wally_bzero(entropy.data(), entropy.size());
    });
}

}
