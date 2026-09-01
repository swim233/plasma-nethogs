// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

/**
 * Publishes plasma-nethogsd's JSON snapshot to QML, woken by the daemon's own
 * writes rather than polling for them.
 *
 * This is the only place coupled to how the daemon publishes its data.
 *
 * It is C++ because QML cannot read a local file cheaply. XMLHttpRequest
 * refuses a file:// URL unless QML_XHR_ALLOW_FILE_READ=1 is set, which would
 * have to hold for the whole session and would lift the restriction for every
 * QML application the user runs, and Plasma ships no QML bindings for D-Bus or
 * local sockets.
 *
 * What pure QML leaves you with is Plasma's executable data engine running
 * `cat` once per poll, which is what this widget used to do. The cost is not
 * the `cat`: the engine starts it from inside plasmashell, and forking a
 * process that large makes the kernel copy its page tables while holding
 * mmap_lock for write. Measured against a plasmashell with 1 GB resident, one
 * fork stalled it for 11-17 ms -- two thirds to all of a frame at 60 Hz, once
 * per poll -- and because the lock is per-process the stall lands on every
 * thread that touches new memory, not only the one that forked. A widget that
 * reports on the system should not be a measurable part of its jitter.
 */
class StateWatcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    /// Shortest interval between two snapshots handed to QML. See coalescing
    /// in StateWatcher.cpp: this throttles by counting publications, not by
    /// clock, so the accepted snapshots stay evenly spaced.
    Q_PROPERTY(int coalesceMs READ coalesceMs WRITE setCoalesceMs NOTIFY coalesceMsChanged)
    Q_PROPERTY(int staleAfterMs READ staleAfterMs WRITE setStaleAfterMs NOTIFY staleAfterMsChanged)

    /// "ok" | "unavailable" (no daemon) | "stale" (daemon stopped abruptly)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    /// The last accepted snapshot, as the text the daemon wrote, for QML to
    /// JSON.parse. Empty unless the status is "ok".
    ///
    /// Handing over a QVariantMap instead would save that parse and does very
    /// nearly work: a nested QVariantList arrives in QML with a length, an
    /// iterator, a map() and a JSON.stringify() that all behave, but
    /// Array.isArray() on it is false. Nothing in the widget asks that today,
    /// which is the problem -- the first place that does would be wrong with
    /// nothing logged. Parsing the text gives real arrays, and it is the same
    /// parse the widget has always done, so nothing downstream of here had to
    /// change with the transport.
    Q_PROPERTY(QString snapshotJson READ snapshotJson NOTIFY snapshotJsonChanged)
    /// Actual spacing of accepted snapshots, 0 until the first one arrives.
    /// Always a whole multiple of the daemon's publishing interval, which is
    /// what the sparkline windows are drawn against.
    Q_PROPERTY(int intervalMs READ intervalMs NOTIFY intervalMsChanged)

public:
    explicit StateWatcher(QObject *parent = nullptr);

    QString path() const
    {
        return m_path;
    }
    void setPath(const QString &path);

    int coalesceMs() const
    {
        return m_coalesceMs;
    }
    void setCoalesceMs(int ms);

    int staleAfterMs() const
    {
        return m_staleAfterMs;
    }
    void setStaleAfterMs(int ms);

    QString status() const
    {
        return m_status;
    }

    QString snapshotJson() const
    {
        return m_snapshotJson;
    }

    int intervalMs() const
    {
        return m_intervalMs;
    }

Q_SIGNALS:
    void pathChanged();
    void coalesceMsChanged();
    void staleAfterMsChanged();
    void statusChanged();
    void snapshotJsonChanged();
    void intervalMsChanged();

    /// One accepted snapshot. Carries the same text as the property, so a
    /// consumer can use either.
    void updated(const QString &snapshotJson);

private:
    void rearm();
    void read();
    void fail(const QString &status);
    void setStatus(const QString &status);
    void setIntervalMs(int ms);

    QFileSystemWatcher m_watcher;
    /// Defers the read after a path change off the caller's stack; see the
    /// constructor.
    QTimer m_firstRead;
    /// Only runs while the snapshot cannot be read; see kRetryMs.
    QTimer m_retry;
    /// Fires when nothing has been published for staleAfterMs.
    QTimer m_watchdog;

    QString m_path;
    int m_coalesceMs = 0;
    int m_staleAfterMs = 5000;

    QString m_status = QStringLiteral("unavailable");
    QString m_snapshotJson;
    int m_intervalMs = 0;

    qint64 m_lastSeq = -1;
    /// Publications seen since the last one handed to QML.
    int m_skipped = 0;
};
