// SPDX-License-Identifier: GPL-2.0

/*
 * The snapshot reader, which is the widget's whole coupling to the daemon.
 * Everything here fails silently in the widget: a watch that stops reporting
 * looks like a daemon that stopped publishing, and a snapshot that reaches QML
 * in the wrong shape looks like a daemon that sent nothing.
 *
 * Run with:
 *   cmake --build build && ctest --test-dir build --output-on-failure
 */

#include "../plugin/StateWatcher.h"

#include <QDateTime>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{

/// seq of the last snapshot handed on, or -1 if none is being held.
qint64 acceptedSeq(const StateWatcher &watcher)
{
    const QString json = watcher.snapshotJson();
    if (json.isEmpty()) {
        return -1;
    }
    return QJsonDocument::fromJson(json.toUtf8()).object().value(QStringLiteral("seq")).toInteger(-1);
}

} // namespace

class StateWatcherTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void readsASnapshotThatAlreadyExists();
    void waitsUntilEveryPropertyIsSet();
    void noticesEveryRepublication();
    void ignoresARepublishedSeq();
    void reportsAMissingSnapshotAndRecovers();
    void recoversWhenTheDirectoryAppearsLater();
    void goesStaleWhenPublishingStops();
    void reportsALeftoverSnapshotAsStale();
    void coalescesWholePublications();
    void handsPidsToJavaScriptAsAnArray();

private:
    /// Writes a snapshot the way the daemon does: into place by rename, which
    /// is what makes watching the file itself useless.
    void publish(qint64 seq, double intervalSeconds = 0.1, double ageSeconds = 0);
    QString path() const
    {
        return m_dir->filePath(QStringLiteral("state.json"));
    }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void StateWatcherTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void StateWatcherTest::publish(qint64 seq, double intervalSeconds, double ageSeconds)
{
    const QJsonObject app{
        {QStringLiteral("key"), QStringLiteral("/usr/bin/curl")},
        {QStringLiteral("name"), QStringLiteral("curl")},
        {QStringLiteral("desktopId"), QJsonValue::Null},
        {QStringLiteral("icon"), QJsonValue::Null},
        {QStringLiteral("exe"), QStringLiteral("/usr/bin/curl")},
        {QStringLiteral("rx"), 2048},
        {QStringLiteral("tx"), 512},
        {QStringLiteral("pids"), QJsonArray{QJsonObject{{QStringLiteral("pid"), 10},
                                                        {QStringLiteral("comm"), QStringLiteral("curl")},
                                                        {QStringLiteral("rx"), 2048},
                                                        {QStringLiteral("tx"), 512}}}},
    };

    const QJsonObject root{
        {QStringLiteral("v"), 1},
        {QStringLiteral("seq"), seq},
        {QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch() / 1000.0 - ageSeconds},
        {QStringLiteral("interval_s"), intervalSeconds},
        {QStringLiteral("total"), QJsonObject{{QStringLiteral("rx"), 2048}, {QStringLiteral("tx"), 512}}},
        {QStringLiteral("apps"), QJsonArray{app}},
    };

    QSaveFile file(path());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY(file.commit());
}

void StateWatcherTest::readsASnapshotThatAlreadyExists()
{
    publish(1);

    StateWatcher watcher;
    QSignalSpy updated(&watcher, &StateWatcher::updated);
    watcher.setPath(path());

    // The first read lands on the next turn of the event loop, not inside
    // setPath; see the constructor.
    QTRY_COMPARE(watcher.status(), QStringLiteral("ok"));
    QCOMPARE(updated.count(), 1);
    QCOMPARE(acceptedSeq(watcher), 1);
}

void StateWatcherTest::waitsUntilEveryPropertyIsSet()
{
    // QML assigns properties one at a time, in declaration order, and main.qml
    // declares path first. A read taken inside setPath() would therefore run
    // against whatever the other properties still were -- their defaults.
    //
    // The snapshot here is older than the default stale window and well inside
    // the one that is about to be set, so reading too early calls a live daemon
    // stopped. The same ordering also decides whether anything is connected to
    // updated() yet, which is why the widget was blank for its first interval.
    publish(1, 0.1, /*ageSeconds=*/8);

    StateWatcher watcher;
    watcher.setPath(path());
    watcher.setStaleAfterMs(60000);

    // Bounded well under the recovery timer, which would otherwise re-read with
    // the right values a couple of seconds later and hide the whole thing.
    QTRY_COMPARE_WITH_TIMEOUT(watcher.status(), QStringLiteral("ok"), 500);
    QCOMPARE(acceptedSeq(watcher), 1);
}

void StateWatcherTest::noticesEveryRepublication()
{
    // A watch on the snapshot file follows the inode the daemon's rename
    // replaces, so it reports one write and is then dropped. Every update has to
    // be noticed, not just the first, and it has to be noticed by a watch: the
    // timeout here is well inside the retry interval, so a watcher that has
    // stopped watching cannot pass this by falling back on the retry.
    publish(1);

    StateWatcher watcher;
    watcher.setPath(path());
    QTRY_COMPARE(acceptedSeq(watcher), 1);

    QSignalSpy updated(&watcher, &StateWatcher::updated);
    for (qint64 seq = 2; seq <= 4; ++seq) {
        publish(seq);
        QTRY_COMPARE_WITH_TIMEOUT(acceptedSeq(watcher), seq, 500);
    }
    QCOMPARE(updated.count(), 3);
    QCOMPARE(watcher.status(), QStringLiteral("ok"));
}

void StateWatcherTest::ignoresARepublishedSeq()
{
    publish(1);

    StateWatcher watcher;
    watcher.setPath(path());
    QTRY_COMPARE(acceptedSeq(watcher), 1);

    QSignalSpy updated(&watcher, &StateWatcher::updated);
    // Same document again. One write can also raise both the file and the
    // directory signal, which arrives here as the same thing.
    publish(1);
    publish(1);
    QTest::qWait(200);

    QCOMPARE(updated.count(), 0);
    QCOMPARE(watcher.status(), QStringLiteral("ok"));
}

void StateWatcherTest::reportsAMissingSnapshotAndRecovers()
{
    StateWatcher watcher;
    watcher.setPath(path());
    QTRY_COMPARE(watcher.status(), QStringLiteral("unavailable"));
    QVERIFY(watcher.snapshotJson().isEmpty());

    // This is what the watch on the directory buys, and the reason a watch on
    // the snapshot file alone is not enough: there is no file to watch yet, so a
    // widget that starts before the daemon has only the directory to hear it
    // arrive on. The timeout is well inside the retry interval, which would
    // otherwise cover for a missing directory watch and hide it.
    publish(1);
    QTRY_COMPARE_WITH_TIMEOUT(watcher.status(), QStringLiteral("ok"), 500);

    QVERIFY(QFile::remove(path()));
    QTRY_COMPARE_WITH_TIMEOUT(watcher.status(), QStringLiteral("unavailable"), 500);
    // The widget clears its list on anything but "ok", so a snapshot left
    // behind here would be a list nobody can explain.
    QVERIFY(watcher.snapshotJson().isEmpty());
    QCOMPARE(watcher.intervalMs(), 0);
}

void StateWatcherTest::recoversWhenTheDirectoryAppearsLater()
{
    // The daemon's runtime directory does not exist until it first runs, and
    // there is nothing for inotify to watch until it does. Without the retry
    // timer the widget would say "no daemon" until the user logged out.
    const QString sub = m_dir->filePath(QStringLiteral("run"));

    StateWatcher watcher;
    watcher.setPath(sub + QStringLiteral("/state.json"));
    QTRY_COMPARE(watcher.status(), QStringLiteral("unavailable"));

    QVERIFY(QDir().mkpath(sub));
    QSaveFile file(sub + QStringLiteral("/state.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{
                                 {QStringLiteral("v"), 1},
                                 {QStringLiteral("seq"), 1},
                                 {QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch() / 1000.0},
                                 {QStringLiteral("interval_s"), 0.1},
                                 {QStringLiteral("total"), QJsonObject{{QStringLiteral("rx"), 0}, {QStringLiteral("tx"), 0}}},
                                 {QStringLiteral("apps"), QJsonArray{}},
                             })
                   .toJson(QJsonDocument::Compact));
    QVERIFY(file.commit());

    QTRY_VERIFY_WITH_TIMEOUT(watcher.status() == QStringLiteral("ok"), 8000);
}

void StateWatcherTest::goesStaleWhenPublishingStops()
{
    StateWatcher watcher;
    watcher.setStaleAfterMs(400);
    watcher.setPath(path());

    publish(1);
    QTRY_COMPARE(watcher.status(), QStringLiteral("ok"));

    // Nothing renames anything, so nothing wakes the watcher: the deadline is
    // the only thing that can notice.
    QTRY_VERIFY_WITH_TIMEOUT(watcher.status() == QStringLiteral("stale"), 3000);
}

void StateWatcherTest::reportsALeftoverSnapshotAsStale()
{
    // A daemon killed with SIGKILL leaves its last snapshot behind, and the
    // widget must not present it as current.
    publish(1, 0.1, /*ageSeconds=*/60);

    StateWatcher watcher;
    watcher.setStaleAfterMs(500);
    watcher.setPath(path());

    QTRY_COMPARE(watcher.status(), QStringLiteral("stale"));
    QVERIFY(watcher.snapshotJson().isEmpty());
}

void StateWatcherTest::coalescesWholePublications()
{
    StateWatcher watcher;
    // Twice the daemon's own interval, so every second publication is kept.
    watcher.setCoalesceMs(200);
    watcher.setPath(path());

    QSignalSpy updated(&watcher, &StateWatcher::updated);
    for (qint64 seq = 1; seq <= 4; ++seq) {
        publish(seq, 0.1);
        // Waiting on a signal is no good here, because half of these are
        // expected to produce none. The pause has to be long enough for each
        // publication to be seen and counted before the next replaces it.
        QTest::qWait(150);
    }

    QCOMPARE(updated.count(), 2);
    QCOMPARE(acceptedSeq(watcher), 3);
    // What the sparkline windows are drawn against, so it has to be the real
    // spacing of the samples and not the configured interval.
    QCOMPARE(watcher.intervalMs(), 200);
}

void StateWatcherTest::handsPidsToJavaScriptAsAnArray()
{
    // main.qml sends the pid list on to the model as JSON text, because a
    // ListModel turns an assigned array into a nested ListModel that the
    // delegate cannot read back. That only works while the list is a real
    // array.
    //
    // Handing QML a QVariantMap instead of text would pass every assertion
    // below except the two Array.isArray ones -- a nested QVariantList has a
    // length, an iterator, a map() and a JSON.stringify() that all behave. Those
    // two are the ones that would catch the change.
    publish(1);

    StateWatcher watcher;
    watcher.setPath(path());
    QTRY_COMPARE(watcher.status(), QStringLiteral("ok"));

    // The same step main.qml takes on the snapshot it is handed.
    QJSEngine engine;
    engine.globalObject().setProperty(QStringLiteral("text"), watcher.snapshotJson());
    const QJSValue snapshot = engine.evaluate(QStringLiteral("JSON.parse(text)"));
    QVERIFY(!snapshot.isError());
    engine.globalObject().setProperty(QStringLiteral("s"), snapshot);

    QVERIFY(engine.evaluate(QStringLiteral("Array.isArray(s.apps)")).toBool());
    QVERIFY(engine.evaluate(QStringLiteral("Array.isArray(s.apps[0].pids)")).toBool());
    QCOMPARE(engine.evaluate(QStringLiteral("JSON.stringify(s.apps[0].pids.map(p => p.pid))")).toString(),
             QStringLiteral("[10]"));
    // The daemon writes JSON null for an unknown icon, which main.qml absorbs
    // with `app.icon || ""`.
    QCOMPARE(engine.evaluate(QStringLiteral("s.apps[0].icon || \"none\"")).toString(),
             QStringLiteral("none"));
    QCOMPARE(engine.evaluate(QStringLiteral("s.total.rx")).toInt(), 2048);
}

QTEST_GUILESS_MAIN(StateWatcherTest)

#include "tst_statewatcher.moc"
