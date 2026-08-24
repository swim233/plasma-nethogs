// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <QString>

struct Snapshot;

/// Publishes a snapshot as JSON at a fixed path.
///
/// The write is atomic (temporary file plus rename), so the plasmoid polling
/// the path can never observe a half-written document and needs no locking.
class JsonWriter
{
public:
    explicit JsonWriter(QString path)
        : m_path(std::move(path))
    {
    }

    bool write(const Snapshot &snapshot, QString *error) const;
    void remove() const;

    QString path() const
    {
        return m_path;
    }

private:
    QString m_path;
};
