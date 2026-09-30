// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "ChildProcess.h"

#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>

namespace {
    // One job for all of Biscuit's helpers, kept open for the life of the
    // app: when Biscuit exits, however it exits, the handle closes and
    // Windows ends every process in the job.
    HANDLE biscuitJob() {
        static const HANDLE job = [] {
            HANDLE handle = CreateJobObjectW(nullptr, nullptr);
            if (handle) {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &info, sizeof(info));
            }
            return handle;
        }();
        return job;
    }
}
#endif

namespace ChildProcess {

void endWithBiscuit(QProcess *process) {
#ifdef Q_OS_WIN
    QObject::connect(process, &QProcess::started, process, [process] {
        const HANDLE job = biscuitJob();
        if (!job) {
            return;
        }
        const HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process->processId()));
        if (child) {
            AssignProcessToJobObject(job, child);
            CloseHandle(child);
        }
    });
#else
    Q_UNUSED(process)
#endif
}

}
