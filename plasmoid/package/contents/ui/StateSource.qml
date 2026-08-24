import QtQuick

import org.kde.plasma.plasma5support as P5Support

/**
 * Reads pnmd's JSON snapshot.
 *
 * This is the only file coupled to how the daemon publishes its data.
 *
 * The obvious approach — XMLHttpRequest on a file:// URL — does not work:
 * Qt refuses local-file reads from QML unless QML_XHR_ALLOW_FILE_READ=1 is set
 * in the environment, and that would have to be set for the whole session,
 * lifting the restriction for every QML application the user runs. Plasma
 * ships no QML bindings for D-Bus or local sockets either, so the remaining
 * options are the executable data engine used here, or a compiled QML plugin.
 *
 * The cost of this one is a `cat` per poll. The upgrade path, if that ever
 * matters, is a C++ QML plugin that watches the file directly — it would
 * replace this file and nothing else.
 */
Item {
    id: root

    property string path: "/run/pnmd/state.json"
    property int pollInterval: 1000
    /// A snapshot older than this means the daemon stopped without cleaning up.
    property int staleAfterMs: 5000

    /// "ok" | "unavailable" (no daemon) | "stale" (daemon stopped abruptly)
    readonly property alias status: internal.status
    readonly property alias snapshot: internal.snapshot

    signal updated(var snapshot)

    /// The engine splits this with POSIX shell rules but does not run a shell,
    /// so quoting is enough to keep an odd path from becoming several arguments.
    readonly property string command: "cat '" + path.replace(/'/g, "'\\''") + "'"

    QtObject {
        id: internal

        property string status: "unavailable"
        property var snapshot: null
        property int lastSeq: -1
    }

    P5Support.DataSource {
        engine: "executable"
        connectedSources: [root.command]
        interval: root.pollInterval


        onNewData: (sourceName, data) => {
            if (data["exit code"] !== 0) {
                internal.status = "unavailable";
                return;
            }

            const text = data["stdout"];
            if (!text) {
                internal.status = "unavailable";
                return;
            }

            let parsed;
            try {
                parsed = JSON.parse(text);
            } catch (error) {
                // A torn read is impossible — the daemon renames into place —
                // so this only happens if something else owns the path.
                internal.status = "unavailable";
                return;
            }

            if (!parsed || parsed.v !== 1) {
                internal.status = "unavailable";
                return;
            }

            if (Date.now() - parsed.ts * 1000 > root.staleAfterMs) {
                internal.status = "stale";
                return;
            }

            internal.status = "ok";

            // Polling and publishing run at the same nominal rate but drift
            // against each other, so the same snapshot turns up twice now and
            // then.
            if (parsed.seq === internal.lastSeq) {
                return;
            }
            internal.lastSeq = parsed.seq;
            internal.snapshot = parsed;
            root.updated(parsed);
        }
    }
}
