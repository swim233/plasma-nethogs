// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "BpfCollector.h"

class ProcessResolver;

struct PidEntry {
    quint32 pid = 0;
    QString comm;
    double rx = 0; ///< bytes/second
    double tx = 0; ///< bytes/second
};

struct AppEntry {
    QString key;
    QString name;
    QString desktopId;
    QString icon;
    QString exePath;
    double rx = 0; ///< bytes/second
    double tx = 0; ///< bytes/second
    QList<PidEntry> pids;

    double total() const
    {
        return rx + tx;
    }
};

struct Snapshot {
    quint64 seq = 0;
    double timestamp = 0; ///< unix seconds
    double intervalSeconds = 0;
    double totalRx = 0; ///< across every process, not just the reported ones
    double totalTx = 0;
    QList<AppEntry> apps; ///< sorted by rx+tx, longest first, capped at topCount
};

/// Turns the BPF map's cumulative counters into per-application rates.
class Aggregator
{
public:
    explicit Aggregator(int topCount)
        : m_topCount(topCount)
    {
    }

    /// Returns false on the very first call, which only primes the baseline —
    /// there is no previous sample to diff against yet.
    bool update(const QList<RawSample> &samples, ProcessResolver &resolver, Snapshot *out);

    /// Processes that vanished since the last tick. Their BPF map entries
    /// should be released.
    const QSet<quint32> &deadPids() const
    {
        return m_deadPids;
    }

private:
    struct Counters {
        quint64 rx = 0;
        quint64 tx = 0;
    };

    int m_topCount;
    quint64 m_seq = 0;
    QElapsedTimer m_clock;
    QHash<quint32, Counters> m_previous;
    QSet<quint32> m_deadPids;
};
