# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

A KDE Plasma 6 widget showing which applications are using the network, fed by
an eBPF collector. Two independent halves: `pnmd`, a privileged C++/QtCore
daemon, and a QML plasmoid. They are coupled only by a JSON snapshot file.

`README.md` documents the measurement boundaries and the user-facing settings;
this file covers building on it.

## Build and install

Needs `bpftool` (Arch: `pacman -S bpf`) besides the Qt6/KF6/Plasma/libbpf/clang
development packages.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
sudo systemctl daemon-reload && sudo systemctl enable --now pnmd
```

`vmlinux.h` and the BPF skeleton are generated at build time from the running
kernel's BTF; CO-RE means the result is not tied to the kernel it was built on.

## Development loop

The two halves iterate separately, which matters because only the daemon needs
root.

**Daemon** — run it by hand instead of restarting the service:

```sh
sudo ./build/bin/pnmd --verbose --interval 1000 --out /tmp/state.json
```

**Plasmoid** — install to `~/.local` (no root) and view it standalone:

```sh
kpackagetool6 --type Plasma/Applet --upgrade plasmoid/package
plasmoidviewer -a io.github.cloudnyko.procnetmonitor
```

A `~/.local` copy shadows the `/usr` one, and `plasmashell` caches QML — changes
to an already-placed widget need `systemctl --user restart plasma-plasmashell`.
Check for two installed copies before concluding a change did not take effect.

## Checks

```sh
# QML syntax
qmllint -I /usr/lib/qt6/qml plasmoid/package/contents/ui/*.qml

# right-click propagation regression test (see "Traps" below)
QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner -input plasmoid/autotests

# the kcfg schema must stay well-formed; a broken one fails silently
xmllint --noout plasmoid/package/contents/config/main.xml

# rebuild translation catalogues after editing a .po
./plasmoid/translations/build.sh
```

## Architecture

```
eBPF ─ fexit probes accumulate per-tgid counters in an LRU hash
  │
pnmd ─ diffs the counters, resolves /proc identity, groups by application,
  │    writes /run/pnmd/state.json atomically once a second
  │
plasmoid ─ reads the snapshot, applies exclusions and the linger window,
           reconciles a ListModel so each change animates
```

**Daemon** (`daemon/src/`). `BpfCollector` owns the BPF object and reads the
map. `ProcessResolver` turns a tgid into an identity, cached by pid and
validated against the process start time so a recycled pid cannot inherit the
previous occupant's identity. `Aggregator` diffs cumulative counters into rates
using a monotonic clock, groups them, and ranks them. `JsonWriter` publishes via
`QSaveFile`. Snapshots carry a monotonic `seq` the widget uses to skip repeats.

**Grouping** is by executable path, not cgroup. systemd splits Chrome across two
cgroups — the browser in `app-com.google.Chrome-<pid>.scope`, its children in
`app-google\x2dchrome@<hash>.service` — but all of them share
`/opt/google/chrome/chrome`. The cgroup identity is used only for the desktop id
(and through it the icon), and only as a fallback grouping key for generic
interpreters, where the executable would merge unrelated programs.

**Widget** (`plasmoid/package/contents/ui/`). `main.qml` owns all state and hands
itself to the two representations explicitly — Plasma does not inject a root
reference, and a representation in its own file cannot see ids from `main.qml`.
`ingest()` applies exclusions, maintains the sparkline history and the linger
map, then `syncModel()` reconciles the `ListModel` in place. `StateSource.qml`
is the only file coupled to the transport.

## Traps

Every one of these fails silently. They are the reason this widget looks
different from the obvious way to write it.

**Hook asymmetry is deliberate.** Receive uses `sock_recvmsg`, one hook covering
everything. Send cannot mirror it: since Linux 6.7 the syscall path calls a
static `__sock_sendmsg()` that the compiler inlines away, so a probe on the
exported `sock_sendmsg` attaches successfully and then reports almost nothing.
Send therefore hooks the transport handlers (`tcp_sendmsg`, `udp_sendmsg`,
`udpv6_sendmsg`), reached through `sk->sk_prot->sendmsg`, an indirect call that
cannot be inlined. The trap is that the receive direction keeps working, so the
numbers look merely low rather than broken.

**Some QtQuick Controls swallow right clicks**, which is how Plasma raises the
Configure/Remove menu — the widget keeps working and nothing is logged.
`PlasmaExtras.Representation`, `PlasmaExtras.PlasmoidHeading` and
`PlasmaComponents.ScrollView` are all in that group, and all three are the
obvious components to build a full representation from. Check anything new that
spans the applet area against `plasmoid/autotests/tst_eventpropagation.qml`.

**A `--` inside an XML comment is illegal**, and one in `config/main.xml` makes
the whole kcfg schema fail to parse. Every `Plasmoid.configuration.*` read then
returns `undefined` and the widget quietly falls back to its QML-side defaults.
Run `xmllint` after editing it.

**`file://` XMLHttpRequest does not work in QML.** Qt refuses local-file reads
unless `QML_XHR_ALLOW_FILE_READ=1` is set, which would have to be session-wide.
Hence the `executable` data engine in `StateSource.qml`, at the cost of one
`cat` per poll. A compiled QML plugin is the upgrade path if that ever matters;
it would replace that one file.

**A `ListModel` cannot round-trip a JS array** — it converts one into a nested
`ListModel` with `count` instead of `length`, and `dynamicRoles` does not help.
The pid list and sparkline samples travel as JSON text; see `rowFor()`.

**Qt logging goes to the journal**, not to a redirected stderr. Prefix with
`QT_ASSUME_STDERR_HAS_CONSOLE=1` or nothing is captured. `/usr/bin/qml` is Qt 5
here; the Qt 6 runtime is `/usr/lib/qt6/bin/qml`.

## Conventions

**Animation belongs to the list, not to the measurements.** Rows animate on
add, remove, rank change and displacement. Readings and the sparkline scale are
never eased toward a new value: the tween would run for as long as a row takes
to slide, so every rank change would come with its numbers racing, and an eased
sparkline scale draws samples against a peak that is not the real one.

Durations come from `Animation.scaled(Kirigami.Units.<base>Duration,
Plasmoid.configuration.animationSpeed)`, keeping the system-wide animation
speed as the baseline, and the on/off switch drives the `enabled` property of
each Transition and Behavior rather than a zero duration. New animations should
follow both.

`syncModel()` removes departed rows before reordering, so an expiring row fades
where it stands rather than being shuffled to the tail first. A steady list must
emit no model operations at all.

**Validate accounting against an application's own byte tally**, not against
interface counters. Interface counters include protocol overhead, and on a
machine with a local proxy the traffic crosses the host twice, so the comparison
is not meaningful. A local transfer where the sender reports its own total is
exact to within the per-second rounding.

**Translations**: `.po` sources in `plasmoid/translations/`, compiled `.mo`
committed under `contents/locale/`. `kpackagetool6` installs a package as-is and
will not run gettext, so the catalogues cannot be generated at install time.
