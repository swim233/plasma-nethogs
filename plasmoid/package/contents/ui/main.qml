import QtQuick

import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import "Rates.js" as Rates

PlasmoidItem {
    id: root

    /// Sparkline window, in samples.
    readonly property int historyLength: 40

    readonly property bool binaryUnits: Plasmoid.configuration.binaryUnits

    /// "ok" | "unavailable" | "stale". Not named `status` to stay clear of
    /// Plasmoid.status.
    readonly property string daemonStatus: source.status

    property real totalRx: 0
    property real totalTx: 0

    /// The representations live in their own QML files and cannot see ids from
    /// this one, so they reach the model through Plasmoid.rootItem.
    readonly property alias apps: appsModel

    /// Top application after exclusions, or null when nothing is transferring.
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

    Plasmoid.backgroundHints: PlasmaCore.Types.DefaultBackground | PlasmaCore.Types.ConfigurableBackground

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

    toolTipMainText: i18n("Network Top")
    toolTipSubText: {
        if (daemonStatus === "unavailable") {
            return i18n("The pnmd service is not running.");
        }
        if (daemonStatus === "stale") {
            return i18n("The pnmd service stopped responding.");
        }
        if (!topApp) {
            return i18n("No network activity.");
        }
        return i18n("%1 — down %2, up %3", topApp.name,
                    Rates.format(topApp.rx, binaryUnits),
                    Rates.format(topApp.tx, binaryUnits));
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
            topApp = null;
            totalRx = 0;
            totalTx = 0;
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
        const kept = [];
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
                kept.push(app);
            }
        }

        totalRx = Math.max(0, rx);
        totalTx = Math.max(0, tx);

        const seen = {};
        for (const app of kept) {
            seen[app.key] = true;
            const samples = history[app.key] || [];
            samples.push(app.rx + app.tx);
            while (samples.length > historyLength) {
                samples.shift();
            }
            history[app.key] = samples;
        }

        // An app that goes quiet keeps its history — padded with zeroes — so
        // its sparkline still has context if it wakes up again. Once the whole
        // window is idle there is nothing left to draw.
        for (const key in history) {
            if (seen[key]) {
                continue;
            }
            const samples = history[key];
            samples.push(0);
            while (samples.length > historyLength) {
                samples.shift();
            }
            if (Rates.peak(samples) === 0) {
                delete history[key];
            }
        }

        const visible = kept.slice(0, Plasmoid.configuration.topCount);
        syncModel(visible);
        topApp = visible.length > 0 ? rowFor(visible[0]) : null;
    }

    /// One model row.
    ///
    /// The two array-valued fields travel as JSON text. A ListModel turns an
    /// assigned array into a nested ListModel, which the delegate cannot read
    /// back as an array — and switching the model to dynamicRoles does not
    /// help. Encoding forty numbers and a handful of pid records once a second
    /// costs nothing and keeps the roles statically typed.
    function rowFor(app) {
        return {
            key: app.key,
            name: app.name,
            icon: app.icon || "",
            exe: app.exe || "",
            desktopId: app.desktopId || "",
            rx: app.rx,
            tx: app.tx,
            pidCount: (app.pids || []).length,
            pidsJson: JSON.stringify(app.pids || []),
            sparkJson: JSON.stringify(history[app.key] || [])
        };
    }

    /// Reconciles the model in place instead of clearing it, so rows that only
    /// change rank slide rather than blink, and an expanded row stays expanded.
    function syncModel(apps) {
        for (let i = 0; i < apps.length; ++i) {
            const app = apps[i];

            let found = -1;
            for (let j = i; j < appsModel.count; ++j) {
                if (appsModel.get(j).key === app.key) {
                    found = j;
                    break;
                }
            }

            if (found === -1) {
                appsModel.insert(i, rowFor(app));
                continue;
            }

            if (found !== i) {
                appsModel.move(found, i, 1);
            }
            appsModel.set(i, rowFor(app));
        }

        if (appsModel.count > apps.length) {
            appsModel.remove(apps.length, appsModel.count - apps.length);
        }
    }
}
