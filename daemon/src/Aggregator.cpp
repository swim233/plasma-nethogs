// SPDX-License-Identifier: GPL-2.0

#include "Aggregator.h"

#include "ProcessResolver.h"

#include <QDateTime>

#include <algorithm>

namespace
{

/// A multi-process app can span several cgroups — Chrome's browser process
/// sits in app-com.google.Chrome-<pid>.scope while its children land in
/// app-google\x2dchrome@<hash>.service — so the group's displayed identity is
/// pinned to the lowest pid rather than to whichever process happened to be
/// iterated first. Otherwise the label and icon would flip between ticks.
struct GroupState {
    AppEntry entry;
    quint32 namePid = 0;
    quint32 desktopPid = 0; ///< lowest pid that actually had a desktop id
};

} // namespace

bool Aggregator::update(const QList<RawSample> &samples, ProcessResolver &resolver, Snapshot *out)
{
    m_deadPids.clear();

    if (!m_clock.isValid()) {
        // First tick: record the baseline and report nothing. Otherwise the
        // first sample would be reported as a rate over an unknown interval.
        m_clock.start();
        for (const RawSample &sample : samples) {
            m_previous.insert(sample.tgid, Counters{sample.rx, sample.tx});
        }
        return false;
    }

    const double elapsed = m_clock.restart() / 1000.0;
    if (elapsed <= 0) {
        return false;
    }

    QHash<QString, GroupState> groups;
    QHash<quint32, Counters> current;
    QSet<quint32> livePids;

    double totalRx = 0;
    double totalTx = 0;

    for (const RawSample &sample : samples) {
        current.insert(sample.tgid, Counters{sample.rx, sample.tx});

        const Counters previous = m_previous.value(sample.tgid);
        // A counter that went backwards means the LRU map evicted the entry
        // and the process re-created it, so the current value *is* the delta.
        const quint64 deltaRx = sample.rx >= previous.rx ? sample.rx - previous.rx : sample.rx;
        const quint64 deltaTx = sample.tx >= previous.tx ? sample.tx - previous.tx : sample.tx;

        const double rx = double(deltaRx) / elapsed;
        const double tx = double(deltaTx) / elapsed;

        totalRx += rx;
        totalTx += tx;

        const ProcInfo *info = resolver.resolve(sample.tgid, sample.comm);
        if (info) {
            livePids.insert(sample.tgid);
        } else {
            // Exited within this window. Its bytes still count, but the map
            // entry is now garbage.
            m_deadPids.insert(sample.tgid);
        }

        if (deltaRx == 0 && deltaTx == 0) {
            continue;
        }

        const QString key = info ? info->appKey : QStringLiteral("comm:") + sample.comm;

        GroupState &group = groups[key];
        if (group.entry.key.isEmpty()) {
            group.entry.key = key;
            group.namePid = sample.tgid;
            group.entry.name = info ? info->name : sample.comm;
            if (info) {
                group.entry.exePath = info->exePath;
            }
        } else if (sample.tgid < group.namePid) {
            group.namePid = sample.tgid;
            group.entry.name = info ? info->name : sample.comm;
            if (info) {
                group.entry.exePath = info->exePath;
            }
        }

        // Tracked separately: the lowest pid in a group does not necessarily
        // have a desktop id, and an empty one must not overwrite a good one.
        if (info && !info->desktopId.isEmpty()
            && (group.desktopPid == 0 || sample.tgid < group.desktopPid)) {
            group.desktopPid = sample.tgid;
            group.entry.desktopId = info->desktopId;
            group.entry.icon = info->icon;
        }

        group.entry.rx += rx;
        group.entry.tx += tx;
        group.entry.pids.append(PidEntry{sample.tgid, info ? info->comm : sample.comm, rx, tx});
    }

    m_previous = std::move(current);
    resolver.prune(livePids);

    QList<AppEntry> apps;
    apps.reserve(groups.size());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        AppEntry &entry = it->entry;
        std::sort(entry.pids.begin(), entry.pids.end(), [](const PidEntry &a, const PidEntry &b) {
            return (a.rx + a.tx) > (b.rx + b.tx);
        });
        apps.append(std::move(entry));
    }

    std::sort(apps.begin(), apps.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.total() > b.total();
    });
    if (apps.size() > m_topCount) {
        apps.resize(m_topCount);
    }

    out->seq = ++m_seq;
    out->timestamp = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    out->intervalSeconds = elapsed;
    out->totalRx = totalRx;
    out->totalTx = totalTx;
    out->apps = std::move(apps);

    return true;
}
