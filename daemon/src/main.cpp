// SPDX-License-Identifier: GPL-2.0

#include "Aggregator.h"
#include "BpfCollector.h"
#include "JsonWriter.h"
#include "ProcessResolver.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QTimer>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

// Qt suppresses info-level output by default; this category is the daemon's
// only channel to the journal, so opt it in.
Q_LOGGING_CATEGORY(log, "plasma-nethogsd", QtInfoMsg)

namespace
{

int g_signalFds[2] = {-1, -1};

void forwardSignal(int)
{
    const char byte = 1;
    // Async-signal-safe; the notifier below turns this into a Qt event.
    [[maybe_unused]] const ssize_t ignored = ::write(g_signalFds[1], &byte, 1);
}

/// Routes SIGINT/SIGTERM into the event loop so the state file gets removed
/// and libbpf detaches cleanly instead of the kernel reaping us.
void installSignalHandler(QCoreApplication *app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFds) != 0) {
        return;
    }

    auto *notifier = new QSocketNotifier(g_signalFds[0], QSocketNotifier::Read, app);
    QObject::connect(notifier, &QSocketNotifier::activated, app, [app] {
        char byte = 0;
        [[maybe_unused]] const ssize_t ignored = ::read(g_signalFds[0], &byte, 1);
        app->quit();
    });

    ::signal(SIGINT, forwardSignal);
    ::signal(SIGTERM, forwardSignal);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("plasma-nethogsd"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Per-process network monitor daemon. Samples socket-layer traffic with "
                       "eBPF and publishes a JSON snapshot for the Plasma widget."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption intervalOption(
        QStringLiteral("interval"), QStringLiteral("Sampling interval in milliseconds."),
        QStringLiteral("ms"), QStringLiteral("1000"));
    const QCommandLineOption outOption(
        QStringLiteral("out"), QStringLiteral("Path of the JSON snapshot to publish."),
        QStringLiteral("path"), QStringLiteral("/run/plasma-nethogsd/state.json"));
    const QCommandLineOption topOption(
        QStringLiteral("top"), QStringLiteral("Number of applications to publish."),
        QStringLiteral("n"), QStringLiteral("20"));
    const QCommandLineOption verboseOption(QStringLiteral("verbose"),
                                           QStringLiteral("Log every snapshot to stderr."));

    parser.addOption(intervalOption);
    parser.addOption(outOption);
    parser.addOption(topOption);
    parser.addOption(verboseOption);
    parser.process(app);

    bool ok = false;
    const int interval = parser.value(intervalOption).toInt(&ok);
    if (!ok || interval < 100) {
        qCCritical(log) << "--interval must be an integer of at least 100 ms.";
        return 2;
    }

    const int topCount = parser.value(topOption).toInt(&ok);
    if (!ok || topCount < 1) {
        qCCritical(log) << "--top must be a positive integer.";
        return 2;
    }

    const bool verbose = parser.isSet(verboseOption);
    const JsonWriter writer(parser.value(outOption));

    // systemd's RuntimeDirectory= normally creates this; do it ourselves too so
    // running plasma-nethogsd by hand for debugging works.
    const QDir parentDir = QFileInfo(writer.path()).absoluteDir();
    if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
        qCCritical(log) << "Cannot create" << parentDir.absolutePath();
        return 1;
    }

    BpfCollector collector;
    QString error;
    if (!collector.load(&error)) {
        qCCritical(log).noquote() << error;
        return 1;
    }

    qCInfo(log).noquote() << QStringLiteral("Attached. Publishing %1 every %2 ms to %3.")
                                 .arg(topCount)
                                 .arg(interval)
                                 .arg(writer.path());

    ProcessResolver resolver;
    Aggregator aggregator(topCount);

    QTimer timer;
    timer.setInterval(interval);
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        Snapshot snapshot;
        if (!aggregator.update(collector.poll(), resolver, &snapshot)) {
            return; // first tick only establishes the baseline
        }

        for (const quint32 pid : aggregator.deadPids()) {
            collector.forget(pid);
        }

        QString writeError;
        if (!writer.write(snapshot, &writeError)) {
            qCWarning(log).noquote() << writeError;
            return;
        }

        if (verbose) {
            const QString top = snapshot.apps.isEmpty()
                ? QStringLiteral("(idle)")
                : QStringLiteral("%1 %2 B/s").arg(snapshot.apps.first().name).arg(
                      qRound64(snapshot.apps.first().total()));
            qCInfo(log).noquote() << QStringLiteral("seq=%1 apps=%2 total=%3 B/s top=%4")
                                         .arg(snapshot.seq)
                                         .arg(snapshot.apps.size())
                                         .arg(qRound64(snapshot.totalRx + snapshot.totalTx))
                                         .arg(top);
        }
    });
    timer.start();

    installSignalHandler(&app);
    const int result = app.exec();

    // Leave no stale snapshot behind: the widget should report the daemon as
    // stopped rather than freezing on the last reading.
    writer.remove();
    return result;
}
