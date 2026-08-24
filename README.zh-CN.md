# plasma-nethogs

一个 KDE Plasma 6 桌面小部件，用于显示当前正在使用网络的应用，后端由 eBPF 采集器支撑。

Linux 没有按进程统计网络字节数的内核接口：`/proc/<pid>/net/*` 是按网络命名空间而非进程划分的，而 `/proc/<pid>/io` 只覆盖磁盘。Plasma 自带的 `ksystemstats` 网络插件只暴露接口级传感器，`plasma-systemmonitor` 的进程表也没有网络列。因此，在 Plasma 桌面上回答"到底是什么在吃我的带宽？"目前只能打开终端运行 `nethogs`。这个项目就是为了填补这个空白。

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

## 它测量什么，以及不测量什么

这是一个"谁在用带宽"的工具，不是流量计费工具。

**计入：** 所有用户的进程，TCP 和 UDP（因此也包括 QUIC）、IPv4 和 IPv6，所有经过套接字的流量。

**不计入或存在偏差：**

1. **仅限应用层字节。** 钩子挂在套接字层，因此统计的是应用程序交给内核、或从内核取走的字节数。与应用程序自身的字节统计相比是精确的：一次 37 GB 的本地传输误差在 3 字节以内（每秒速率取整为整数所致）。与接口计数器相比则总是偏低，因为不包含 IP/TCP 头部、重传和 TLS 记录开销——所以它永远不会与 `ip -s link` 一致；在流量经过本地代理的机器上，两者甚至不能直接比较。
2. **从套接字 `splice()` 会绕过 `sock_recvmsg`**，接收方向会被漏掉。发送方向没问题：现代内核会把 `sendfile()` 路由到带 `MSG_SPLICE_PAGES` 的 `sendmsg`，从而到达 `tcp_sendmsg`。
3. **回环流量按目的地址排除**（`127.0.0.0/8`、`::1`、`::ffff:127.0.0.0/8`）。否则本地开发服务器会永远排在列表最前面。
4. **TUN 模式代理会使每个字节翻倍。** 使用 clash/mihomo/sing-box 的 TUN 模式时，路径是 `应用 → tun → 代理 → 网卡`，两段都是真实的套接字流量，因此总量约为真实值的 2 倍。在套接字层无法区分二者。请使用小部件设置中的 *排除进程*；配置页提供了常见代理名称的一键建议。
5. **虚拟机**显示为单个 `qemu-system-*` 行。虚拟机内部的进程不可见，这是正确的。
6. **容器**归属正确——守护进程看到的是宿主机 pid——但显示的名称是容器自身的 `comm`，cgroup 分组会归到 `docker-*` / `libpod-*` 下。
7. **内核侧流量**（NFS 客户端、kTLS、iSCSI）和**转发流量**（路由/NAT，从不经过本地套接字）没有归属进程，不会显示。
8. **退出的进程会失去其行。** 它死掉的那个 tick 内字节数仍会被统计；BPF map 在计数器旁存了 `comm`，所以即使 `/proc/<pid>` 已消失，名称也能保留。

## 为什么钩子不对称

接收方向只有一个钩子：`sock_recvmsg()` 是所有套接字读取汇入的唯一入口，因此一次覆盖 TCP、UDP、QUIC、IPv4 和 IPv6，其返回值就是拷贝到用户空间的字节数。

发送方向无法照搬。自 Linux 6.7 起，系统调用路径调用的是静态的 `__sock_sendmsg()`，编译器会将其内联消除；导出的 `sock_sendmsg()` 只剩内核内部的调用方在使用。对它的探针能成功挂载，但几乎不报告任何数据——这是一个容易踩的坑，因为接收方向一直正常，数字看起来只是偏低而非损坏。传输层处理函数（`tcp_sendmsg`、`udp_sendmsg`、`udpv6_sendmsg`）通过 `sk->sk_prot->sendmsg` 这个间接调用到达，无法被内联，因此是稳定的挂载点。`tcp_sendmsg` 同时服务两个地址族；UDP 每个地址族需要一个钩子，IPv6 那个在挂载时是可选的（针对未编译 IPv6 的内核）。

## 应用如何分组

可执行文件路径是主键，因为真正把浏览器的多个进程合并起来的是它。systemd 把 Chrome 拆到两个 cgroup 中——浏览器进程落在 `app-com.google.Chrome-<pid>.scope`，而它的子进程进入 `app-google\x2dchrome@<hash>.service`——但它们都共享 `/opt/google/chrome/chrome`。

对于通用解释器（`python*`、`node`、`java`、`electron*`、shell 等），直接合并会混入无关程序，此时改用 systemd cgroup 身份，最后才用 `comm` 兜底。展开一行可以看到被合并进来的各个 pid。

图标取自与 cgroup 桌面 id 匹配的系统级 `.desktop` 文件中的 `Icon=`——根据进程名猜测不够可靠，因为 Chrome 的桌面 id 是 `com.google.Chrome`、二进制名是 `chrome`、图标却是 `google-chrome`。安装在 `~/.local/share` 下的应用会退回到通用图标：unit 设置了 `ProtectHome=yes`，root 守护进程没有理由去读家目录来渲染图标。

## 构建与安装

### Arch 上，通过 AUR

```sh
paru -S plasma-nethogs-git      # 或 yay，或其他任何 AUR 助手
```

或者不用助手：

```sh
git clone https://aur.archlinux.org/plasma-nethogs-git.git
cd plasma-nethogs-git
makepkg -si
```

它是 `-git` 包，因为目前还没有打标签的发布版本：`pkgver()` 解析为构建时对应的提交，因此升级意味着针对 `main` 当前内容重新构建。

构建会读取 `/sys/kernel/btf/vmlinux` 来生成 `vmlinux.h`，所以**构建**机器需要 `CONFIG_DEBUG_INFO_BTF=y`——Arch 自带内核满足此条件，`devtools` chroot 也可以（它们会挂载 `/sys`）。CO-RE 在加载时针对运行中的内核解析重定位，因此产出的包并不绑定构建它的内核。

### 从源码构建

依赖：Qt 6 Core、KF6（Package）、ECM、Plasma 6 开发文件、libbpf ≥ 1.0、clang 和 `bpftool`。在 Arch 上，`bpftool` 位于 `bpf` 包中：

```sh
sudo pacman -S bpf
```

然后：

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
sudo systemctl daemon-reload
```

`packaging/` 存放两个 PKGBUILD，`packaging/arch` 构建的是它所在的检出目录而非拉取源码——`cd packaging/arch && makepkg -si` 会得到一个由 pacman 跟踪的包，相比裸 `cmake --install` 更容易卸载干净。

### 首次运行

```sh
sudo systemctl enable --now plasma-nethogsd
```

把 *Plasma NetHogs* 添加到面板或桌面。右键可打开常规的 配置 / 移除 菜单。

### 设置

| 设置项 | 默认值 | 含义 |
| --- | --- | --- |
| 标题 | *Plasma NetHogs* | 列表上方的标题文字；可隐藏 |
| 列出的应用数 | 5 | 列表容纳的行数 |
| 刷新间隔 | 1000 ms | 应与守护进程自身的间隔一致 |
| 空闲应用保留时间 | 10 s | 安静应用淡出前的宽限期 |
| 动画 | 开 | 列表变化是否带动画 |
| 速度 | 1× | 在系统全局动画速度之上缩放动画时长 |
| 排除进程 | 空 | 要排除的名称，同时从总量中减去 |
| 面板条目显示 | 最繁忙的应用 | 紧凑面板条目显示的内容 |
| 快照文件 | `/run/plasma-nethogsd/state.json` | 读取守护进程输出的位置 |

停止传输的应用会保留在列表中的原位置，变暗并显示为 0，直到宽限期结束。按每秒变化的速率排名会让突发性应用每秒闪烁进出；而活跃应用总是优先占据可用行，因此残留的应用永远不会挤掉正在传输的应用。

内核必须启用 `CONFIG_DEBUG_INFO_BTF=y`；如果 `/sys/kernel/btf/vmlinux` 缺失，`plasma-nethogsd` 会明确提示。`vmlinux.h` 在构建时从构建机的 BTF 生成，但 CO-RE 重定位针对运行中的内核解析，因此二进制并不绑定构建它的内核。

### 一个值得知道的坑

当右键点击穿透到小部件下方的 containment 时，Plasma 会显示小部件的 配置 / 移除 菜单。但若干 QtQuick Controls 会吞掉按下事件，菜单就永远不出现：小部件照常工作，日志里也没有任何记录。`PlasmaExtras.Representation`、`PlasmaExtras.PlasmoidHeading` 和 `PlasmaComponents.ScrollView` 都在此列——而它们恰恰是构建完整 representation 时最容易想到的三个组件。

`FullRepresentation.qml` 避开了它们，改用普通 `Item` 根元素、`KSvg.FrameSvgItem` 标题和带附加 `ScrollBar` 的裸 `ListView`。`plasmoid/autotests/tst_eventpropagation.qml` 逐个探测每个组件，并断言最终结构能让右键点击穿透：

```sh
QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner -input plasmoid/autotests
```

任何横跨 applet 区域的新增内容都应先用它验证。

### 迭代小部件

plasmoid 可以单独安装且无需 root，迭代速度快得多：

```sh
kpackagetool6 --type Plasma/Applet --upgrade plasmoid/package
plasmoidviewer -a io.github.swim233.plasma-nethogs
```

QML 警告默认进入 journal，除非另行指定：

```sh
QT_ASSUME_STDERR_HAS_CONSOLE=1 plasmoidviewer -a io.github.swim233.plasma-nethogs
```

## 权限

`plasma-nethogsd` 作为系统服务运行，只带三个 capability，不多不少：

- `CAP_BPF` 和 `CAP_PERFMON`——加载并挂载程序。
- `CAP_SYS_PTRACE`——对其他用户的 `/proc/<pid>/exe` 执行 `readlink()`。

unit 还设置了 `ProtectSystem=strict`、`ProtectHome=yes`、`NoNewPrivileges=yes`、capability 边界集和系统调用过滤器。

`/run/plasma-nethogsd/state.json` 是全局可读的，因为小部件以桌面用户身份运行。在多登录用户的机器上，这会暴露哪些应用在用网络。若要限制，在 unit 中设置 `RuntimeDirectoryMode=0750`，并让桌面用户属于某个组。

## 为什么小部件读文件

Plasma 6 没有提供 D-Bus 或本地套接字的 QML 绑定，而 Qt 拒绝在未设置 `QML_XHR_ALLOW_FILE_READ=1` 时用 `XMLHttpRequest` 读取 `file://`——该变量需要在整个会话范围内设置，等于为用户运行的每个 QML 应用解除限制。因此小部件使用 `executable` 数据引擎，每次轮询付出一次 `cat` 的开销。

守护进程用 `QSaveFile` 写入（临时文件加 rename），读方永远不会看到写了一半的文档；每份快照还带一个单调递增的 `seq`，小部件用它跳过重复内容。

如果将来这成为问题，升级路径是编写一个直接监视文件的编译型 QML 插件。它只需替换 `contents/ui/StateSource.qml`——该文件是唯一与传输方式耦合的文件。

## 快照格式

`/run/plasma-nethogsd/state.json`，版本 1。所有速率均为**字节每秒**。

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

应用的 `rx`/`tx` 恰好是其 `pids` 之和。而 `total` 覆盖机器上的所有进程，不只是列出的那些，因此通常大于列出的应用之和。`desktopId`、`icon` 和 `exe` 可能为 `null`。

## 开销

在 7.1 内核上、守护进程每秒采样一次测得：

| | 空闲 | 约 3 GB/s 的套接字流量 |
| --- | --- | --- |
| CPU | 单核的 0.12% | 单核的 0.17% |

开销几乎不随流量变化，因为每次调用的工作在核内只是一次 map 自增，用户空间每个 tick 只读一次 map。常驻内存的大部分是 QtCore；cgroup 自身的峰值约 8 MB。

## 守护进程选项

| 选项 | 默认值 | 含义 |
| --- | --- | --- |
| `--interval <ms>` | 1000 | 采样间隔；与小部件的设置保持一致 |
| `--out <path>` | `/run/plasma-nethogsd/state.json` | 快照输出位置 |
| `--top <n>` | 20 | 每份快照发布的应用数 |
| `--verbose` | 关 | 记录每份快照 |

手动运行是单独检查采集器最快的方式：

```sh
sudo ./build/bin/plasma-nethogsd --verbose --interval 1000 --out /tmp/state.json
```