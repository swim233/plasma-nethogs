import QtQuick

import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import "Rates.js" as Rates

PlasmoidItem {
    id: root

    /// Sparkline window, in samples. Every window holds exactly this many, so
    /// all of them span the same stretch of time; see pushSample().
    ///
    /// One sample arrives per snapshot, so the configured span buys resolution
    /// or reach depending on the refresh interval, never both. Two samples is
    /// the least a trace can be drawn from. The upper clamp is only a guard
    /// against a hand-edited config: the widest span at the shortest interval
    /// stays under it.
    readonly property int historyLength: Math.max(2, Math.min(1200,
        Math.round(Plasmoid.configuration.historySeconds * 1000
                   / Math.max(1, Plasmoid.configuration.refreshInterval))))

    readonly property bool binaryUnits: Plasmoid.configuration.binaryUnits

    /// A kcfg default cannot be translated, so an empty setting means "use the
    /// translated default".
    readonly property string title: Plasmoid.configuration.widgetTitle.length > 0
        ? Plasmoid.configuration.widgetTitle
        : i18n("Plasma NetHogs")

    /// "ok" | "unavailable" | "stale". Not named `status` to stay clear of
    /// Plasmoid.status.
    readonly property string daemonStatus: source.status

    property real totalRx: 0
    property real totalTx: 0

    /// The representations live in their own QML files and cannot see ids from
    /// this one, so they reach the model through an explicitly passed reference.
    readonly property alias apps: appsModel

    /// Busiest *active* application, or null when nothing is transferring. A
    /// lingering row is never reported here — the panel should not claim an
    /// idle application is the top talker.
    /// Assigned rather than bound: the model's contents change far more often
    /// than its count, which a binding on get(0) would miss.
    property var topApp: null

    /// Lower-cased names the user wants left out. Kept widget-side so editing
    /// the list takes effect without restarting the daemon.
    readonly property var excluded: (Plasmoid.configuration.excludeProcesses || [])
        .map(entry => String(entry).trim().toLowerCase())
        .filter(entry => entry.length > 0)

    // Keyed by app key; each value is an array of recent total rates. Not a
    // property binding source, so every consumer gets its copy through the
    // model row instead.
    property var history: ({})

    // Keyed by app key: the last row seen for an application and when it was
    // last active. Lets a bursty application hold its place instead of
    // flickering out of the list between transfers.
    property var lingering: ({})

    Plasmoid.backgroundHints: PlasmaCore.Types.DefaultBackground | PlasmaCore.Types.ConfigurableBackground
    Plasmoid.title: root.title

    preferredRepresentation: Plasmoid.formFactor === PlasmaCore.Types.Planar
        ? fullRepresentation
        : compactRepresentation

    // Representations are separate components and cannot see this file's ids,
    // so hand them the root item explicitly. Plasma does not inject it.
    compactRepresentation: CompactRepresentation {
        main: root
    }

    fullRepresentation: FullRepresentation {
        main: root
    }

    toolTipMainText: root.title
    toolTipSubText: {
        if (daemonStatus === "unavailable") {
            return i18n("The plasma-nethogsd service is not running.");
        }
        if (daemonStatus === "stale") {
            return i18n("The plasma-nethogsd service stopped responding.");
        }
        if (!topApp) {
            return i18n("No network activity.");
        }
        return i18n("%1 — up %2, down %3", topApp.name,
                    Rates.format(topApp.tx, binaryUnits),
                    Rates.format(topApp.rx, binaryUnits));
    }

    ListModel {
        id: appsModel
    }

    StateSource {
        id: source

        path: Plasmoid.configuration.statePath
        // Each poll forks a process, so match the daemon's rate rather than
        // oversampling it; a duplicate snapshot is cheaper than a spare fork.
        pollInterval: Math.max(500, Plasmoid.configuration.refreshInterval)
        staleAfterMs: Plasmoid.configuration.refreshInterval * 5

        onUpdated: snapshot => root.ingest(snapshot)
    }

    onDaemonStatusChanged: {
        if (daemonStatus !== "ok") {
            appsModel.clear();
            history = {};
            lingering = {};
            topApp = null;
            totalRx = 0;
            totalTx = 0;
        }
    }

    // Data collected under a setting's old value does not survive changing it:
    // lingering rows would otherwise outlive the grace period that created
    // them, and a sparkline window would end up holding samples taken one
    // second apart next to samples taken five seconds apart, on an axis that
    // claims they are evenly spaced.
    Connections {
        target: Plasmoid.configuration

        function onLingerSecondsChanged() {
            root.lingering = {};
        }

        function onExcludeProcessesChanged() {
            root.lingering = {};
        }

        function onRefreshIntervalChanged() {
            root.history = {};
        }
    }

    function isExcluded(app) {
        if (excluded.length === 0) {
            return false;
        }
        const name = String(app.name || "").toLowerCase();
        const exe = app.exe ? String(app.exe).split("/").pop().toLowerCase() : "";
        return excluded.indexOf(name) !== -1 || (exe.length > 0 && excluded.indexOf(exe) !== -1);
    }

    function ingest(snapshot) {
        const active = [];
        let rx = snapshot.total.rx;
        let tx = snapshot.total.tx;

        for (const app of snapshot.apps) {
            if (isExcluded(app)) {
                // The daemon's totals cover every process, so subtract what the
                // user asked to hide. An excluded app outside the daemon's top
                // list was contributing too little to matter.
                rx -= app.rx;
                tx -= app.tx;
            } else {
                active.push(app);
            }
        }

        totalRx = Math.max(0, rx);
        totalTx = Math.max(0, tx);

        const now = Date.now();
        const lingerMs = Math.max(0, Plasmoid.configuration.lingerSeconds) * 1000;

        const isActive = {};
        for (const app of active) {
            isActive[app.key] = true;
            lingering[app.key] = { row: app, lastActive: now };
            pushSample(app.key, app.rx + app.tx);
        }

        // An app that goes quiet keeps its history — padded with zeroes — so
        // its sparkline still has context if it wakes up again. Once the whole
        // window is idle there is nothing left to draw.
        for (const key in history) {
            if (isActive[key]) {
                continue;
            }
            if (Rates.peak(pushSample(key, 0)) === 0) {
                delete history[key];
            }
        }

        // Applications that stopped transferring but are still inside their
        // grace period, most recently active first.
        const idle = [];
        for (const key in lingering) {
            if (isActive[key]) {
                continue;
            }
            if (now - lingering[key].lastActive > lingerMs) {
                delete lingering[key];
                continue;
            }
            idle.push(lingering[key]);
        }
        idle.sort((a, b) => b.lastActive - a.lastActive);

        // Active applications claim the available slots first, so a lingering
        // row can never push a transferring one out of the list.
        const rows = active.map(app => rowFor(app, false))
            .concat(idle.map(entry => rowFor(entry.row, true)))
            .slice(0, Plasmoid.configuration.topCount);

        syncModel(rows);
        topApp = active.length > 0 ? rowFor(active[0], false) : null;
    }

    /// Appends one reading to an application's sparkline window and returns it.
    ///
    /// A window is created full length, zero filled, rather than growing from
    /// empty as its application is observed: every window then covers the same
    /// span of time, samples enter at the right edge and the oldest one falls
    /// off the left. Growing windows are drawn against the same width, so an
    /// application seen four samples ago would spread those four across the
    /// width its neighbour uses for a full window, and its trace would keep
    /// contracting until it filled up — two rows on different time axes.
    ///
    /// Every known key is advanced by exactly one sample per snapshot, active
    /// or not, which is what keeps the windows aligned with each other.
    ///
    /// Widening the configured span pads on the left and narrowing it drops
    /// from the left, so a window follows the setting from the next snapshot on
    /// without the graphs having to start over.
    function pushSample(key, value) {
        const samples = history[key] || new Array(historyLength).fill(0);
        samples.push(value);
        while (samples.length > historyLength) {
            samples.shift();
        }
        while (samples.length < historyLength) {
            samples.unshift(0);
        }
        history[key] = samples;
        return samples;
    }

    /// One model row.
    ///
    /// The two array-valued fields travel as JSON text. A ListModel turns an
    /// assigned array into a nested ListModel, which the delegate cannot read
    /// back as an array — and switching the model to dynamicRoles does not
    /// help. Encoding a window's worth of numbers and a handful of pid records
    /// once a second costs nothing and keeps the roles statically typed.
    function rowFor(app, idle) {
        return {
            key: app.key,
            name: app.name,
            icon: app.icon || "",
            exe: app.exe || "",
            desktopId: app.desktopId || "",
            // A lingering row reports zero rather than its last reading, which
            // would look like the transfer is still going.
            rx: idle ? 0 : app.rx,
            tx: idle ? 0 : app.tx,
            idle: idle,
            pidCount: (app.pids || []).length,
            pidsJson: JSON.stringify(app.pids || []),
            sparkJson: JSON.stringify(history[app.key] || [])
        };
    }

    /// Reconciles the model in place instead of clearing it. Every kind of
    /// change then maps onto the ListView transition that fits it: a new
    /// application animates in, a rank change slides, and an expired row
    /// fades out — rather than the whole list blinking once a second.
    function syncModel(rows) {
        const wanted = {};
        for (const row of rows) {
            wanted[row.key] = true;
        }

        // Departing rows go first, so each one fades out where it stands.
        // Trimming the tail instead would shuffle an expiring row down the
        // list before removing it, and it would read as a demotion rather
        // than as an exit.
        for (let i = appsModel.count - 1; i >= 0; --i) {
            if (!wanted[appsModel.get(i).key]) {
                appsModel.remove(i, 1);
            }
        }

        // Every surviving row is now somewhere in `rows`, and `rows` has no
        // duplicate keys, so this pass alone brings the model into shape.
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i];

            let found = -1;
            for (let j = i; j < appsModel.count; ++j) {
                if (appsModel.get(j).key === row.key) {
                    found = j;
                    break;
                }
            }

            if (found === -1) {
                appsModel.insert(i, row);
                continue;
            }

            if (found !== i) {
                appsModel.move(found, i, 1);
            }
            appsModel.set(i, row);
        }
    }
}
