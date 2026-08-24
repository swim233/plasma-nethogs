// SPDX-License-Identifier: GPL-2.0

#include "ProcessResolver.h"

#include <QFile>
#include <QFileInfo>
#include <QLatin1StringView>

#include <array>
#include <climits>
#include <unistd.h>

namespace
{

/// Binaries that host arbitrary programs. Grouping by executable path would
/// merge unrelated apps for these, so they fall through to the cgroup unit.
bool isGenericInterpreter(QStringView base)
{
    static constexpr std::array kExact = {
        QLatin1StringView("node"),   QLatin1StringView("nodejs"), QLatin1StringView("deno"),
        QLatin1StringView("bun"),    QLatin1StringView("java"),   QLatin1StringView("ruby"),
        QLatin1StringView("perl"),   QLatin1StringView("php"),    QLatin1StringView("sh"),
        QLatin1StringView("bash"),   QLatin1StringView("zsh"),    QLatin1StringView("dash"),
        QLatin1StringView("fish"),   QLatin1StringView("mono"),   QLatin1StringView("dotnet"),
        QLatin1StringView("gjs"),    QLatin1StringView("qml"),    QLatin1StringView("qmlscene"),
        QLatin1StringView("wine"),   QLatin1StringView("wine64"), QLatin1StringView("busybox"),
    };

    for (const auto &name : kExact) {
        if (base == name) {
            return true;
        }
    }

    // python, python3, python3.13, electron, electron33, ...
    return base.startsWith(QLatin1StringView("python")) || base.startsWith(QLatin1StringView("electron"));
}

/// systemd escapes unit names: '-' becomes \x2d, and so on.
QString unescapeSystemd(const QString &in)
{
    if (!in.contains(QLatin1StringView("\\x"))) {
        return in;
    }

    QString out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        if (in[i] == u'\\' && i + 3 < in.size() && in[i + 1] == u'x') {
            bool ok = false;
            const uint code = QStringView(in).mid(i + 2, 2).toUInt(&ok, 16);
            if (ok) {
                out.append(QChar(code));
                i += 3;
                continue;
            }
        }
        out.append(in[i]);
    }
    return out;
}

bool isAllDigits(QStringView s)
{
    if (s.isEmpty()) {
        return false;
    }
    for (const QChar c : s) {
        if (!c.isDigit()) {
            return false;
        }
    }
    return true;
}

QString readSmallFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLocal8Bit(file.read(64 * 1024));
}

/// Last path component of the cgroup v2 hierarchy, e.g.
/// "app-com.google.Chrome-228577.scope".
QString readCgroupLeaf(quint32 pid)
{
    const QString content = readSmallFile(QStringLiteral("/proc/%1/cgroup").arg(pid));
    if (content.isEmpty()) {
        return {};
    }

    QString line;
    for (const QStringView candidate : QStringView(content).split(u'\n', Qt::SkipEmptyParts)) {
        if (candidate.startsWith(QLatin1StringView("0::"))) { // the unified hierarchy
            line = candidate.toString();
            break;
        }
        if (line.isEmpty()) {
            line = candidate.toString();
        }
    }

    const qsizetype slash = line.lastIndexOf(u'/');
    return slash < 0 ? QString() : line.mid(slash + 1);
}

/// Splits a cgroup leaf into a desktop id (for apps) or a unit name.
///
/// Real shapes seen on a Plasma session:
///   app-com.google.Chrome-228577.scope              -> com.google.Chrome
///   app-google\x2dchrome@73675e5c....service        -> google-chrome
///   app-org.kde.kclockd-autostart@autostart.service -> org.kde.kclockd-autostart
///   plasma-plasmashell.service                      -> unit plasma-plasmashell
///   main.scope / session-1.scope / init.scope       -> nothing useful
void parseCgroupIdentity(const QString &leaf, QString *desktopId, QString *unit)
{
    if (leaf.endsWith(QLatin1StringView(".scope"))) {
        QString base = leaf.chopped(6);
        if (base.startsWith(QLatin1StringView("app-"))) {
            base = base.mid(4);
            // systemd appends the launching pid; drop it so every launch of
            // the same app produces the same id.
            const qsizetype dash = base.lastIndexOf(u'-');
            if (dash > 0 && isAllDigits(QStringView(base).mid(dash + 1))) {
                base = base.left(dash);
            }
            *desktopId = unescapeSystemd(base);
        } else if (base.startsWith(QLatin1StringView("docker-")) //
                   || base.startsWith(QLatin1StringView("libpod-")) //
                   || base.startsWith(QLatin1StringView("crio-"))) {
            *unit = base;
        }
        return;
    }

    if (leaf.endsWith(QLatin1StringView(".service"))) {
        QString base = leaf.chopped(8);
        if (const qsizetype at = base.indexOf(u'@'); at >= 0) {
            base = base.left(at); // strip the systemd instance suffix
        }
        if (base.startsWith(QLatin1StringView("app-"))) {
            *desktopId = unescapeSystemd(base.mid(4));
        } else {
            *unit = unescapeSystemd(base);
        }
    }
}

/// Field 22 of /proc/<pid>/stat. Parsed from the last ')' because the comm
/// field can itself contain spaces and parentheses.
bool readStartTime(quint32 pid, quint64 *startTime)
{
    const QString stat = readSmallFile(QStringLiteral("/proc/%1/stat").arg(pid));
    const qsizetype close = stat.lastIndexOf(u')');
    if (close < 0) {
        return false;
    }

    const auto fields = QStringView(stat).mid(close + 1).split(u' ', Qt::SkipEmptyParts);
    // fields[0] is 'state', so starttime (field 22) is index 19.
    if (fields.size() <= 19) {
        return false;
    }

    bool ok = false;
    *startTime = fields[19].toULongLong(&ok);
    return ok;
}

QString readExe(quint32 pid)
{
    const QByteArray link = QStringLiteral("/proc/%1/exe").arg(pid).toLocal8Bit();
    std::array<char, PATH_MAX> buffer{};
    const ssize_t len = ::readlink(link.constData(), buffer.data(), buffer.size() - 1);
    if (len <= 0) {
        return {}; // process gone, or we lack CAP_SYS_PTRACE for another user
    }

    QString path = QString::fromLocal8Bit(buffer.data(), len);
    // The kernel appends this once the binary is replaced, e.g. after an update.
    if (path.endsWith(QLatin1StringView(" (deleted)"))) {
        path.chop(10);
    }
    return path;
}

/// Where system-wide .desktop files live. The user's own
/// ~/.local/share/applications is deliberately out of reach: the unit sets
/// ProtectHome=yes, and a root daemon has no business reading home
/// directories to render an icon. Apps installed there fall back to the
/// generic icon in the widget.
const std::array kApplicationDirs = {
    QLatin1StringView("/usr/share/applications"),
    QLatin1StringView("/usr/local/share/applications"),
    QLatin1StringView("/var/lib/flatpak/exports/share/applications"),
};

QString parseIconKey(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    bool inDesktopEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            // Only the main group counts; actions further down have their own
            // Icon= keys.
            if (inDesktopEntry) {
                break;
            }
            inDesktopEntry = (line == QLatin1StringView("[Desktop Entry]"));
            continue;
        }
        if (inDesktopEntry && line.startsWith(QLatin1StringView("Icon="))) {
            return line.mid(5).trimmed();
        }
    }
    return {};
}

} // namespace

QString ProcessResolver::iconFor(const QString &desktopId)
{
    if (desktopId.isEmpty()) {
        return {};
    }

    if (const auto it = m_iconCache.constFind(desktopId); it != m_iconCache.constEnd()) {
        return *it; // may legitimately be empty: misses are cached too
    }

    QString icon;
    for (const auto &dir : kApplicationDirs) {
        const QString path = QStringLiteral("%1/%2.desktop").arg(QLatin1StringView(dir), desktopId);
        icon = parseIconKey(path);
        if (!icon.isEmpty()) {
            break;
        }
    }

    m_iconCache.insert(desktopId, icon);
    return icon;
}

const ProcInfo *ProcessResolver::resolve(quint32 pid, const QString &bpfComm)
{
    quint64 startTime = 0;
    if (!readStartTime(pid, &startTime)) {
        return nullptr; // the process exited
    }

    if (const auto it = m_cache.constFind(pid); it != m_cache.constEnd() && it->startTime == startTime) {
        return &it->info;
    }

    ProcInfo info;

    // bpf_get_current_comm() reports the *thread* name, which for a threaded
    // app is something like "Chrome_ChildIOT". /proc/<tgid>/comm is the
    // process name, so prefer it and keep the BPF one only as a fallback for
    // processes that already exited.
    info.comm = readSmallFile(QStringLiteral("/proc/%1/comm").arg(pid)).trimmed();
    if (info.comm.isEmpty()) {
        info.comm = bpfComm;
    }

    info.exePath = readExe(pid);

    QString unit;
    parseCgroupIdentity(readCgroupLeaf(pid), &info.desktopId, &unit);

    // Grouping key. Executable path first: it is what actually merges Chrome's
    // dozen processes, which systemd splits across two different cgroups
    // (app-com.google.Chrome-<pid>.scope for the browser,
    // app-google\x2dchrome@<hash>.service for the children).
    const QString exeBase = info.exePath.isEmpty() ? QString() : QFileInfo(info.exePath).fileName();

    if (!info.exePath.isEmpty() && !isGenericInterpreter(exeBase)) {
        info.appKey = QStringLiteral("exe:") + info.exePath;
    } else if (!info.desktopId.isEmpty()) {
        info.appKey = QStringLiteral("app:") + info.desktopId;
    } else if (!unit.isEmpty()) {
        info.appKey = QStringLiteral("unit:") + unit;
    } else if (!info.exePath.isEmpty()) {
        info.appKey = QStringLiteral("exe:") + info.exePath;
    } else {
        info.appKey = QStringLiteral("comm:") + info.comm;
    }

    info.name = exeBase.isEmpty() ? info.comm : exeBase;
    info.icon = iconFor(info.desktopId);

    Entry entry;
    entry.startTime = startTime;
    entry.info = std::move(info);
    const auto inserted = m_cache.insert(pid, std::move(entry));
    return &inserted->info;
}

void ProcessResolver::prune(const QSet<quint32> &livePids)
{
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        it = livePids.contains(it.key()) ? std::next(it) : m_cache.erase(it);
    }
}
