// SPDX-License-Identifier: GPL-2.0

#include "StateWatcher.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace
{

/// Cadence of the recovery timer. This is not a poll: it runs only while the
/// snapshot cannot be read -- normally because the daemon's runtime directory
/// does not exist yet, so there is nothing for inotify to watch -- and stops on
/// the first successful read. It also covers a watch going missing, which
/// QFileSystemWatcher does not report.
constexpr int kRetryMs = 2000;

/// The daemon reports the interval it measured rather than the one it was
/// configured with, so the value lands a few milliseconds either side of a
/// round number. Anything keyed off it -- the coalescing factor, the sparkline
/// time axis -- has to see a stable number or it churns on that jitter alone.
int quantizedPublishMs(double intervalSeconds)
{
    return qMax(1, qRound(intervalSeconds * 10.0)) * 100;
}

} // namespace

StateWatcher::StateWatcher(QObject *parent)
    : QObject(parent)
{
    m_retry.setInterval(kRetryMs);
    m_watchdog.setSingleShot(true);

    // The first read is deferred to the next turn of the event loop rather than
    // taken inside setPath(), because QML assigns properties one at a time and
    // attaches signal handlers as it goes. Reading immediately would read with
    // whatever coalesceMs and staleAfterMs happened to be set already -- their
    // defaults, if path is declared first -- and would emit updated() before
    // anything was connected to it, losing the first snapshot and leaving the
    // widget blank until the second one.
    m_firstRead.setSingleShot(true);
    m_firstRead.setInterval(0);
    connect(&m_firstRead, &QTimer::timeout, this, &StateWatcher::read);

    // Both signals mean the same thing here: look again. Which one arrives
    // depends on how the write happened, and a single write can raise both.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &StateWatcher::read);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &StateWatcher::read);
    connect(&m_retry, &QTimer::timeout, this, &StateWatcher::read);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("stale"));
    });
}

void StateWatcher::setPath(const QString &path)
{
    if (m_path == path) {
        return;
    }
    m_path = path;

    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }

    m_lastSeq = -1;
    m_skipped = 0;
    setIntervalMs(0);

    Q_EMIT pathChanged();
    m_firstRead.start();
}

void StateWatcher::setCoalesceMs(int ms)
{
    if (m_coalesceMs == ms) {
        return;
    }
    m_coalesceMs = ms;
    // The factor is derived from the next publication rather than recomputed
    // here, which would need a publishing interval that may not have been seen
    // yet.
    m_skipped = 0;
    Q_EMIT coalesceMsChanged();
}

void StateWatcher::setStaleAfterMs(int ms)
{
    if (m_staleAfterMs == ms) {
        return;
    }
    m_staleAfterMs = ms;
    if (m_watchdog.isActive()) {
        m_watchdog.start(m_staleAfterMs);
    }
    Q_EMIT staleAfterMsChanged();
}

void StateWatcher::rearm()
{
    if (m_path.isEmpty()) {
        return;
    }

    // The daemon publishes with QSaveFile, which renames a finished document
    // over the old one. An inotify watch on the file follows the inode that the
    // rename replaces: it fires once and QFileSystemWatcher then drops the path,
    // so such a watch has to be placed again after every single update -- which
    // is what this function does, and on its own it very nearly works.
    //
    // The directory is watched as well because "very nearly" leaves two holes.
    // There is nothing to re-add while the snapshot does not exist, so a widget
    // started before the daemon would not hear it start; and an update landing
    // between the drop and the re-add is missed outright, which is reachable
    // whenever the daemon publishes faster than this runs. A watch on the
    // directory has neither hole, and the two together cost one inotify watch.
    //
    // addPath() on a path that does not exist fails with a warning, and this
    // runs from the retry timer, so both are checked first rather than letting
    // a missing daemon write to the journal every two seconds.
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!m_watcher.directories().contains(dir) && QFileInfo::exists(dir)) {
        m_watcher.addPath(dir);
    }
    if (!m_watcher.files().contains(m_path) && QFileInfo::exists(m_path)) {
        m_watcher.addPath(m_path);
    }
}

void StateWatcher::read()
{
    rearm();

    QFile file(m_path);
    if (m_path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("unavailable"));
        return;
    }

    // Parsed here to decide what to do with it, and parsed again by QML to use
    // it. Keeping the document as text is what makes the second parse produce
    // the real JavaScript arrays the widget is written against; see the note on
    // snapshotJson. Two passes over a few kilobytes on a tmpfs is not a cost
    // worth trading correctness for.
    const QByteArray text = file.readAll();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text, &parseError);
    // A torn read is impossible, because the daemon renames into place, so a
    // parse failure means something else owns the path.
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(QStringLiteral("unavailable"));
        return;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("v")).toInt() != 1) {
        fail(QStringLiteral("unavailable"));
        return;
    }

    const int publishMs = quantizedPublishMs(root.value(QStringLiteral("interval_s")).toDouble());

    // Wall-clock age decides only the reads taken before the daemon has been
    // seen publishing: a snapshot left behind by one that was killed without
    // cleaning up. After that the watchdog detects a stop, which is cheaper and
    // is not a comparison between two wall clocks that the user can step.
    if (!m_watchdog.isActive()) {
        const qint64 written = qint64(root.value(QStringLiteral("ts")).toDouble() * 1000.0);
        if (QDateTime::currentMSecsSinceEpoch() - written > m_staleAfterMs) {
            fail(QStringLiteral("stale"));
            return;
        }
    }

    // The same document can arrive twice: the directory and the file both
    // reporting one write, or a rename that changed nothing. Only a new seq is
    // evidence that the daemon is still running.
    const qint64 seq = root.value(QStringLiteral("seq")).toInteger(-1);
    if (seq == m_lastSeq) {
        return;
    }
    m_lastSeq = seq;

    // refreshInterval is documented to match the interval the daemon runs at,
    // but nothing enforces that, and reporting a healthy daemon as stopped is
    // worse than taking an extra interval to notice a real stop.
    m_watchdog.start(qMax(m_staleAfterMs, 3 * publishMs));
    m_retry.stop();
    setStatus(QStringLiteral("ok"));

    // Coalescing counts publications rather than watching the clock. A time
    // window would reject a snapshot that arrived a millisecond early and then
    // wait out a whole further interval, so the accepted samples would land
    // unevenly -- and the sparkline windows are drawn as though every sample
    // were exactly one interval from the last.
    //
    // intervalMs is zero only before the first accepted snapshot, which is
    // handed over immediately rather than after a full coalescing window.
    const int factor = qMax(1, qRound(double(m_coalesceMs) / publishMs));
    if (m_intervalMs > 0 && ++m_skipped < factor) {
        return;
    }
    m_skipped = 0;
    setIntervalMs(factor * publishMs);

    m_snapshotJson = QString::fromUtf8(text);
    Q_EMIT snapshotJsonChanged();
    Q_EMIT updated(m_snapshotJson);
}

void StateWatcher::fail(const QString &status)
{
    m_watchdog.stop();
    m_lastSeq = -1;
    m_skipped = 0;
    setIntervalMs(0);

    if (!m_snapshotJson.isEmpty()) {
        m_snapshotJson.clear();
        Q_EMIT snapshotJsonChanged();
    }

    setStatus(status);

    // Recovery cannot depend on a watch, since the directory holding the
    // snapshot may not exist yet.
    if (!m_retry.isActive()) {
        m_retry.start();
    }
}

void StateWatcher::setStatus(const QString &status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    Q_EMIT statusChanged();
}

void StateWatcher::setIntervalMs(int ms)
{
    if (m_intervalMs == ms) {
        return;
    }
    m_intervalMs = ms;
    Q_EMIT intervalMsChanged();
}
