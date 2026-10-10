// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Keccak-256 for Ethereum: Monero's implementation, compiled here under other
// names so that it never clashes with Monero's own copy in the same binary.
// Same variant as Ethereum (original Keccak padding, not SHA3-256).

// Monero compiles it with _GNU_SOURCE: without it, glibc's <sys/param.h>
// leaves BYTE_ORDER undefined and int-util.h stops the build (GCC, Linux).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define keccak biscuit_keccak
#define keccakf biscuit_keccakf
#define keccak1600 biscuit_keccak1600
#define keccak_init biscuit_keccak_init
#define keccak_update biscuit_keccak_update
#define keccak_finish biscuit_keccak_finish
#define keccakf_rndc biscuit_keccakf_rndc

#include "crypto/keccak.c"
