// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <QHash>
#include <QSet>
#include <QString>

/// Everything pnmd knows about one process. Deliberately no icon: resolving
/// icon names means reading .desktop files out of the user's home, which the
/// systemd unit blocks with ProtectHome=yes. The plasmoid does that lookup
/// instead, where it also gets the user's locale for free.
struct ProcInfo {
    /// Stable identity used to merge processes into one displayed row.
    /// One of "app:<desktop-id>", "unit:<name>", "exe:<path>", "comm:<name>".
    QString appKey;
    QString desktopId; ///< empty when the process was not launched as a desktop app
    QString exePath; ///< empty when /proc/<pid>/exe is unreadable
    QString comm;
    QString name; ///< executable basename, falling back to comm
    /// Icon= from the system-wide .desktop file, empty when there is none.
    /// Guessing from the process name is not good enough: Chrome's desktop id
    /// is com.google.Chrome and its binary is chrome, but its icon is
    /// google-chrome.
    QString icon;
};

/// Reads process identity out of /proc and caches it.
///
/// The cache is keyed by pid but validated against the process start time from
/// /proc/<pid>/stat, so a recycled pid can never inherit the previous
/// occupant's identity. Reading stat is cheap; the cgroup/exe/cmdline parsing
/// it guards is not.
class ProcessResolver
{
public:
    /// Returns nullptr when the process is gone.
    const ProcInfo *resolve(quint32 pid, const QString &bpfComm);

    /// Drops cache entries for pids not in @p livePids.
    void prune(const QSet<quint32> &livePids);

private:
    struct Entry {
        quint64 startTime = 0;
        ProcInfo info;
    };

    /// Looks up Icon= for a desktop id, caching misses as well as hits.
    QString iconFor(const QString &desktopId);

    QHash<quint32, Entry> m_cache;
    QHash<QString, QString> m_iconCache;
};
