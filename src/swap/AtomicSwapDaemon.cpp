// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "AtomicSwapDaemon.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "utils/ChildProcess.h"
#include "utils/config.h"
#include "utils/os/tails.h"
#include "utils/os/whonix.h"

namespace biscuit::swap {

AtomicSwapDaemon::AtomicSwapDaemon(QObject *parent)
    : QObject(parent)
{
}

AtomicSwapDaemon::~AtomicSwapDaemon() {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString AtomicSwapDaemon::systemTorName() {
    if (TailsOS::detect()) {
        return "Tails";
    }
    if (WhonixOS::detect()) {
        return "Whonix";
    }
    return {};
}

QString AtomicSwapDaemon::networkFlag(bool tor) {
    return tor && systemTorName().isEmpty() ? "--tor" : "--clearnet";
}

QString AtomicSwapDaemon::helperPath() {
    const QString fromEnv = qEnvironmentVariable("BISCUIT_SWAPD");
    if (!fromEnv.isEmpty()) {
        return QFileInfo(fromEnv).isExecutable() ? fromEnv : QString();
    }
#ifdef Q_OS_WIN
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath("biscuit-swapd.exe");
#else
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath("biscuit-swapd");
#endif
    // An update left the new helper next to the old one (it may have been
    // running): put it in place before it is used.
    if (QFileInfo::exists(bundled + ".new")) {
        QFile::remove(bundled);
        QFile::rename(bundled + ".new", bundled);
    }
    return QFileInfo(bundled).isExecutable() ? bundled : QString();
}

bool AtomicSwapDaemon::isRunning() const {
    return m_process && m_process->state() != QProcess::NotRunning;
}

void AtomicSwapDaemon::start(bool tor) {
    if (isRunning()) {
        return;
    }

    const QString program = helperPath();
    if (program.isEmpty()) {
        emit failed("The swap helper (biscuit-swapd) was not found.");
        emit finished();
        return;
    }

    // Tor state (guards, directory cache) is kept between runs.
    const QString dataDir = Config::defaultConfigDir().filePath("swapd");
    QDir().mkpath(dataDir);

    m_reportedError = false;
    m_stopping = false;
    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments({"discover", networkFlag(tor), "--data-dir", dataDir});
    // Logs are not needed here, and an unread pipe would eventually block the helper.
    m_process->setStandardErrorFile(QProcess::nullDevice());
    ChildProcess::endWithBiscuit(m_process);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &AtomicSwapDaemon::onReadyRead);
    connect(m_process, &QProcess::finished, this, &AtomicSwapDaemon::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // QProcess sends no finished() in that case.
        if (error == QProcess::FailedToStart) {
            emit failed("The swap helper could not be started.");
            m_process->deleteLater();
            emit finished();
        }
    });

    m_process->start();
}

void AtomicSwapDaemon::stop() {
    if (!isRunning()) {
        return;
    }
    m_stopping = true;
    m_process->terminate();
    // The helper holds no state yet: don't wait long for it.
    QPointer<QProcess> process = m_process;
    QTimer::singleShot(3000, this, [process] {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });
}

void AtomicSwapDaemon::onReadyRead() {
    while (m_process && m_process->canReadLine()) {
        const atomic::Event event = atomic::parseLine(m_process->readLine().trimmed());

        switch (event.type) {
            case atomic::Event::Type::Tor:
                emit torStatusChanged(event.torStatus);
                break;
            case atomic::Event::Type::Started:
                emit discoveryStarted(event.usesTor);
                break;
            case atomic::Event::Type::Summary:
                emit summaryChanged(event.summary);
                break;
            case atomic::Event::Type::Offers:
                emit offersChanged(event.offers);
                break;
            case atomic::Event::Type::Error:
                m_reportedError = true;
                emit failed(event.message);
                break;
            case atomic::Event::Type::Stopped:
            case atomic::Event::Type::Bitcoin:
            case atomic::Event::Type::SwapStarted:
            case atomic::Event::Type::SwapResumed:
            case atomic::Event::Type::SwapState:
            case atomic::Event::Type::SwapFinished:
            case atomic::Event::Type::Ignored:
            case atomic::Event::Type::Invalid:
                break;
        }
    }
}

void AtomicSwapDaemon::onFinished(int exitCode, QProcess::ExitStatus status) {
    onReadyRead();
    // A crash, or a failure the helper could not report itself. Stopping on
    // request ends with a signal, which Qt also reports as a crash.
    const bool abnormal = status == QProcess::CrashExit || exitCode != 0;
    if (abnormal && !m_stopping && !m_reportedError) {
        emit failed("The swap helper stopped unexpectedly.");
    }
    if (m_process) {
        m_process->deleteLater();
    }
    emit finished();
}

}
