// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#ifndef BISCUIT_CHILDPROCESS_H
#define BISCUIT_CHILDPROCESS_H

class QProcess;

namespace ChildProcess {
    // The process ends with Biscuit, even when Biscuit is killed or crashes.
    // Windows: a job object that the system closes with Biscuit. Elsewhere
    // the helpers watch Biscuit themselves (biscuit-swapd checks its parent,
    // Tor gets __OwningControllerProcess), so this does nothing there.
    void endWithBiscuit(QProcess *process);
}

#endif // BISCUIT_CHILDPROCESS_H
