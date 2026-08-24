# plasma-nethogs

A KDE Plasma 6 widget that shows which applications are using the network right
now, backed by an eBPF collector.
[简体中文](README.zh-CN.md)

Linux has no kernel interface for per-process network byte counts:
`/proc/<pid>/net/*` is per network namespace, not per process, and
`/proc/<pid>/io` only covers disk. Plasma's own `ksystemstats` network plugin
exposes interface-level sensors only, and the process table in
`plasma-systemmonitor` has no network column. So answering "what is eating my
bandwidth?" on a Plasma desktop currently means opening a terminal and running
`nethogs`. This fills that gap.

```
┌─ eBPF ─────────────────────────────────────────────┐
│ fexit/tcp_sendmsg    ┐                             │
│ fexit/udp_sendmsg    ├→ tx += bytes                │
│ fexit/udpv6_sendmsg  ┘                             │
│ fexit/sock_recvmsg    → rx += bytes                │
│ LRU_HASH<tgid, {rx, tx, comm}>                     │
└────────────────────┬───────────────────────────────┘
                     │ read once per second
┌────────────────────▼───────────────────────────────┐
│ plasma-nethogsd — privileged daemon (C++/QtCore + libbpf)     │
│ diffs counters, resolves /proc identity, groups by │
│ application, writes /run/plasma-nethogsd/state.json atomically│
└────────────────────┬───────────────────────────────┘
                     │
┌────────────────────▼───────────────────────────────┐
│ plasmoid — QML, reads the snapshot once per second │
└────────────────────────────────────────────────────┘
```

## What it measures, and what it does not

This is a "who is using the bandwidth" tool, not a traffic accounting tool.

**Counted:** processes belonging to every user, TCP and UDP (so QUIC too), IPv4
and IPv6, all traffic that passes through a socket.

**Not counted, or skewed:**

1. **Application-layer bytes only.** The hooks sit at the socket layer, so the
   figure is what the application handed to, or took from, the kernel. Against
   an application's own byte tally it is exact: a 37 GB local transfer was
   accounted to within 3 bytes, the rounding of per-second rates to integers.
   Against an interface counter it will always read low, since IP/TCP headers,
   retransmissions and TLS record overhead are not included — so it will never
   match `ip -s link`, and on a machine whose traffic crosses a local proxy it
   is not directly comparable at all.
2. **`splice()` from a socket bypasses `sock_recvmsg`** and is missed on the
   receive side. The send side is fine: modern kernels route `sendfile()`
   through `sendmsg` with `MSG_SPLICE_PAGES`, which reaches `tcp_sendmsg`.
3. **Loopback is excluded** by destination address (`127.0.0.0/8`, `::1`,
   `::ffff:127.0.0.0/8`). Without this a local development server would sit at
   the top of the list permanently.
4. **A TUN-mode proxy doubles every byte.** With clash/mihomo/sing-box in TUN
   mode the path is `application → tun → proxy → NIC`, and both halves are real
   socket traffic, so the total reads about 2× the truth. There is no way to
   tell the two apart at the socket layer. Use *Exclude processes* in the
   widget's settings; the configuration page offers the usual proxy names as
   one-click suggestions.
5. **Virtual machines** appear as a single `qemu-system-*` row. Processes
   inside the guest are not visible, which is correct.
6. **Containers** are attributed correctly — the daemon sees host pids — but
   the displayed name is the container's own `comm`, and cgroup grouping falls
   under `docker-*` / `libpod-*`.
7. **Kernel-side traffic** (NFS client, kTLS, iSCSI) and **forwarded traffic**
   (routing/NAT, which never touches a local socket) have no owning process and
   are not shown.
8. **A process that exits loses its row.** Its bytes are still counted for the
   tick in which it died; the BPF map stores `comm` alongside the counters so
   the name survives even when `/proc/<pid>` is already gone.

## Why the hooks are asymmetric

Receive is one hook: `sock_recvmsg()` is the single entry point every socket
read funnels through, so it covers TCP, UDP, QUIC, IPv4 and IPv6 at once, and
its return value is the byte count copied to userspace.

Send cannot mirror that. Since Linux 6.7 the syscall path calls a static
`__sock_sendmsg()`, which the compiler inlines away; the exported
`sock_sendmsg()` remains only for in-kernel callers. A probe on it attaches
successfully and then reports almost nothing — an easy trap, because the
receive direction keeps working and the numbers look merely low rather than
broken. The transport handlers (`tcp_sendmsg`, `udp_sendmsg`,
`udpv6_sendmsg`) are reached through `sk->sk_prot->sendmsg`, an indirect call
that cannot be inlined, so they are stable attach points. `tcp_sendmsg` serves
both address families; UDP needs one hook per family, and the IPv6 one is
optional at attach time for kernels built without IPv6.

## How applications are grouped

The executable path is the primary key, because it is what actually merges a
browser's processes. systemd splits Chrome across two cgroups — the browser
process lands in `app-com.google.Chrome-<pid>.scope` while its children go to
`app-google\x2dchrome@<hash>.service` — but all of them share
`/opt/google/chrome/chrome`.

For generic interpreters (`python*`, `node`, `java`, `electron*`, shells, …)
that would merge unrelated programs, the systemd cgroup identity is used
instead, then `comm` as a last resort. Expanding a row shows the individual
pids that were merged into it.

The icon comes from `Icon=` in the system-wide `.desktop` file matching the
cgroup's desktop id — guessing from the process name is not good enough, since
Chrome's desktop id is `com.google.Chrome`, its binary is `chrome`, and its
icon is `google-chrome`. Applications installed under `~/.local/share`
fall back to a generic icon: the unit sets `ProtectHome=yes`, and a root daemon
has no business reading home directories to render an icon.

## Building and installing

### On Arch, from the AUR

```sh
paru -S plasma-nethogs-git      # or yay, or any other helper
```

Or without a helper:

```sh
git clone https://aur.archlinux.org/plasma-nethogs-git.git
cd plasma-nethogs-git
makepkg -si
```

It is a `-git` package because there is no tagged release yet: `pkgver()`
resolves to the commit it was built from, so upgrading means rebuilding against
whatever `main` holds at the time.

The build reads `/sys/kernel/btf/vmlinux` to generate `vmlinux.h`, so the
*building* machine needs `CONFIG_DEBUG_INFO_BTF=y` — Arch's own kernels have it,
and a `devtools` chroot works because those mount `/sys`. CO-RE resolves the
relocations against the running kernel at load time, so the resulting package is
not tied to the kernel that built it.

### From source

Requires: Qt 6 Core, KF6 (Package), ECM, Plasma 6 development files, libbpf ≥
1.0, clang, and `bpftool`. On Arch, `bpftool` is in the `bpf` package:

```sh
sudo pacman -S bpf
```

Then:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
sudo systemctl daemon-reload
```

`packaging/` holds both PKGBUILDs, and `packaging/arch` builds the checkout it
sits in rather than fetching a source — `cd packaging/arch && makepkg -si` gives
a package pacman tracks, which is a good deal easier to take back out again than
a bare `cmake --install`.

### First run

```sh
sudo systemctl enable --now plasma-nethogsd
```

Add *Plasma NetHogs* to a panel or the desktop. Right-click it for the usual
Configure / Remove menu.

### Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| Title | *Plasma NetHogs* | Heading shown above the list; can be hidden |
| Applications to list | 5 | How many rows the list holds |
| Refresh interval | 1000 ms | Should match the daemon's own interval |
| Keep idle applications for | 10 s | Grace period before a quiet application fades out |
| Animations | on | Whether list changes are animated at all |
| Speed | 1× | Scales the durations on top of the system-wide animation speed |
| Exclude processes | empty | Names to leave out, and to subtract from the totals |
| Panel entry shows | busiest application | What the compact panel entry displays |
| Snapshot file | `/run/plasma-nethogsd/state.json` | Where to read the daemon's output |

An application that stops transferring keeps its place in the list, dimmed and
reading zero, until its grace period expires. Ranking by a rate that moves
every second means bursty applications would otherwise flicker in and out once
a second. Active applications always claim the available rows first, so a
lingering one can never displace a transferring one.

The kernel must have `CONFIG_DEBUG_INFO_BTF=y`; `plasma-nethogsd` says so explicitly if
`/sys/kernel/btf/vmlinux` is missing. `vmlinux.h` is generated at build time
from the build machine's BTF, but CO-RE relocations are resolved against the
running kernel, so the binary is not tied to the kernel it was built on.

### A trap worth knowing about

Plasma shows a widget's Configure / Remove menu when a right click reaches the
containment underneath it. Several QtQuick Controls accept the press instead,
and then the menu silently never appears: the widget keeps working, and nothing
is logged. `PlasmaExtras.Representation`, `PlasmaExtras.PlasmoidHeading` and
`PlasmaComponents.ScrollView` are all in that group — and all three are the
obvious components to build a full representation from.

`FullRepresentation.qml` avoids them, using a plain `Item` root, a
`KSvg.FrameSvgItem` heading and a bare `ListView` with an attached `ScrollBar`.
`plasmoid/autotests/tst_eventpropagation.qml` probes each component and asserts
that the resulting structure passes a right click through:

```sh
QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner -input plasmoid/autotests
```

Anything new that spans the applet area should be checked against it first.

### Iterating on the widget

The plasmoid alone can be installed without root, which is much faster to work
with:

```sh
kpackagetool6 --type Plasma/Applet --upgrade plasmoid/package
plasmoidviewer -a io.github.swim233.plasma-nethogs
```

QML warnings are routed to the journal unless you ask otherwise:

```sh
QT_ASSUME_STDERR_HAS_CONSOLE=1 plasmoidviewer -a io.github.swim233.plasma-nethogs
```

## Privileges

`plasma-nethogsd` runs as a system service with three capabilities and no more:

- `CAP_BPF` and `CAP_PERFMON` — load and attach the programs.
- `CAP_SYS_PTRACE` — `readlink()` on another user's `/proc/<pid>/exe`.

The unit also sets `ProtectSystem=strict`, `ProtectHome=yes`,
`NoNewPrivileges=yes`, a capability bounding set, and a syscall filter.

`/run/plasma-nethogsd/state.json` is world-readable, because the widget runs as the
desktop user. On a machine with several login users that discloses which
applications are using the network. To restrict it, set
`RuntimeDirectoryMode=0750` in the unit plus a group the desktop user belongs
to.

## Why the widget reads a file

Plasma 6 ships no QML bindings for D-Bus or local sockets, and Qt refuses
`file://` reads from `XMLHttpRequest` unless `QML_XHR_ALLOW_FILE_READ=1` is set
— which would have to be set session-wide, lifting the restriction for every
QML application the user runs. So the widget uses the `executable` data engine
and pays one `cat` per poll.

The daemon writes with `QSaveFile` (temporary file plus rename), so a reader
can never see a half-written document, and each snapshot carries a monotonic
`seq` the widget uses to skip repeats.

If that ever becomes a problem, the upgrade path is a compiled QML plugin that
watches the file directly. It would replace `contents/ui/StateSource.qml` and
nothing else — that file is the only one coupled to the transport.

## Snapshot format

`/run/plasma-nethogsd/state.json`, version 1. All rates are **bytes per second**.

```json
{
  "v": 1,
  "seq": 1420,
  "ts": 1787539538.148,
  "interval_s": 1.002,
  "total": { "rx": 4922594, "tx": 0 },
  "apps": [
    {
      "key": "exe:/opt/google/chrome/chrome",
      "name": "chrome",
      "desktopId": "com.google.Chrome",
      "icon": "google-chrome",
      "exe": "/opt/google/chrome/chrome",
      "rx": 2788115,
      "tx": 0,
      "pids": [ { "pid": 228577, "comm": "chrome", "rx": 2788115, "tx": 0 } ]
    }
  ]
}
```

An application's `rx`/`tx` is exactly the sum of its `pids`. `total`, however,
covers every process on the machine, not only the ones listed, so it is
normally larger than the listed applications add up to. `desktopId`, `icon` and
`exe` may be `null`.

## Overhead

Measured on a 7.1 kernel with the daemon sampling once a second:

| | idle | ~3 GB/s of socket traffic |
| --- | --- | --- |
| CPU | 0.12% of one core | 0.17% of one core |
| RSS | 19.7 MB | 19.7 MB |

The cost barely moves with traffic, because the per-call work happens in the
kernel as a map increment and userspace only reads the map once per tick. Most
of the resident size is QtCore; the cgroup's own peak is around 8 MB.

## Daemon options

| Option | Default | Meaning |
| --- | --- | --- |
| `--interval <ms>` | 1000 | Sampling interval; match the widget's setting |
| `--out <path>` | `/run/plasma-nethogsd/state.json` | Snapshot location |
| `--top <n>` | 20 | Applications published per snapshot |
| `--verbose` | off | Log each snapshot |

Running it by hand is the quickest way to check the collector on its own:

```sh
sudo ./build/bin/plasma-nethogsd --verbose --interval 1000 --out /tmp/state.json
```
