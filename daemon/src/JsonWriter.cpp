// SPDX-License-Identifier: GPL-2.0

#include "JsonWriter.h"

#include "Aggregator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{

/// Rates are bytes/second; sub-byte precision is noise and only inflates the
/// document.
qint64 rounded(double value)
{
    return qRound64(value);
}

QJsonValue orNull(const QString &value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

} // namespace

bool JsonWriter::write(const Snapshot &snapshot, QString *error) const
{
    QJsonArray apps;
    for (const AppEntry &app : snapshot.apps) {
        QJsonArray pids;
        // Summing the rounded pid values, rather than rounding the group's
        // own total, keeps "the members add up to the row" true. Rounding both
        // independently leaves them off by up to half a byte per member, which
        // is invisible on screen but breaks anyone consuming the JSON.
        qint64 groupRx = 0;
        qint64 groupTx = 0;

        for (const PidEntry &pid : app.pids) {
            const qint64 pidRx = rounded(pid.rx);
            const qint64 pidTx = rounded(pid.tx);
            groupRx += pidRx;
            groupTx += pidTx;

            pids.append(QJsonObject{
                {QStringLiteral("pid"), qint64(pid.pid)},
                {QStringLiteral("comm"), pid.comm},
                {QStringLiteral("rx"), pidRx},
                {QStringLiteral("tx"), pidTx},
            });
        }

        apps.append(QJsonObject{
            {QStringLiteral("key"), app.key},
            {QStringLiteral("name"), app.name},
            {QStringLiteral("desktopId"), orNull(app.desktopId)},
            {QStringLiteral("icon"), orNull(app.icon)},
            {QStringLiteral("exe"), orNull(app.exePath)},
            {QStringLiteral("rx"), groupRx},
            {QStringLiteral("tx"), groupTx},
            {QStringLiteral("pids"), pids},
        });
    }

    const QJsonObject root{
        {QStringLiteral("v"), 1},
        {QStringLiteral("seq"), qint64(snapshot.seq)},
        {QStringLiteral("ts"), snapshot.timestamp},
        {QStringLiteral("interval_s"), snapshot.intervalSeconds},
        {QStringLiteral("total"),
         QJsonObject{
             {QStringLiteral("rx"), rounded(snapshot.totalRx)},
             {QStringLiteral("tx"), rounded(snapshot.totalTx)},
         }},
        {QStringLiteral("apps"), apps},
    };

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Cannot write %1: %2").arg(m_path, file.errorString());
        }
        return false;
    }

    // World-readable on purpose: the plasmoid runs as the desktop user, not as
    // root. See the security note in README.md.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                        | QFileDevice::ReadOther);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));

    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Cannot commit %1: %2").arg(m_path, file.errorString());
        }
        return false;
    }

    return true;
}

void JsonWriter::remove() const
{
    QFile::remove(m_path);
}
