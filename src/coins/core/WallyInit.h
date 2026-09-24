// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_WALLYINIT_H
#define BISCUIT_WALLYINIT_H

namespace biscuit::coins {

// Initializes libwally-core once (thread safe) and randomizes its secp256k1
// context against side-channel attacks. Called by every function using it.
void ensureWallyInit();

}

#endif // BISCUIT_WALLYINIT_H
