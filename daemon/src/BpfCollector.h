// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <QList>
#include <QString>

struct plasma_nethogs_bpf;

/// One process's cumulative byte counters as read straight out of the BPF map.
struct RawSample {
    quint32 tgid = 0;
    quint64 rx = 0;
    quint64 tx = 0;
    QString comm;
};

/// Owns the BPF object: loads it, attaches the two fexit programs, and reads
/// the traffic map. Counters are cumulative since the entry was created; the
/// Aggregator turns them into rates.
class BpfCollector
{
public:
    BpfCollector() = default;
    ~BpfCollector();

    BpfCollector(const BpfCollector &) = delete;
    BpfCollector &operator=(const BpfCollector &) = delete;

    /// Loads and attaches. On failure returns false and fills @p error with a
    /// diagnostic aimed at a human reading `systemctl status plasma-nethogsd`.
    bool load(QString *error);

    QList<RawSample> poll() const;

    /// Drops a dead process's entry so the map does not accumulate garbage.
    void forget(quint32 tgid) const;

private:
    plasma_nethogs_bpf *m_skel = nullptr;
    int m_mapFd = -1;
};
