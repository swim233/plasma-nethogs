# 更新日志

版本变更记录,是 GitHub Release 说明的来源。

## v0.1.0 - 2026-09-06

首个版本。

### Added
- 新增 eBPF 采集器:接收方向挂钩 `sock_recvmsg`,发送方向挂钩 `tcp_sendmsg` / `udp_sendmsg` / `udpv6_sendmsg`,按 tgid 在 LRU 哈希中累计字节数,并在计数器旁保存 `comm` 使已退出进程的名称仍可显示。
- 新增特权守护进程 `plasma-nethogsd`:差分计数器得出速率,从 `/proc` 解析进程身份并按进程启动时间校验(复用的 pid 不会继承旧身份),按应用分组并排序,每秒经 `QSaveFile` 原子写入 `/run/plasma-nethogsd/state.json`,快照带单调递增的 `seq`。
- 新增 Plasma 6 部件:应用列表按速率排序,上行在下行之前读出;每行带同一时刻轴上的历史曲线;展开一行可查看并入的各个 pid,仅剩一个进程时也可收起;面板紧凑条目的显示内容可配置。
- 新增按可执行文件路径分组,通用解释器(`python*`、`node`、`java`、`electron*`、shell 等)改用 systemd cgroup 身份,最后以 `comm` 兜底;图标取自匹配 cgroup 桌面 id 的系统级 `.desktop` 文件。
- 新增标题、空闲应用保留(停止传输的应用保留原位、变暗显示为零,宽限期结束后淡出,活跃应用始终优先占位),以及列表增删、换位、位移的过渡动画;动画可整体开关,速度可配置,读数与曲线缩放不参与补间。
- 新增排除进程设置,排除项同时从总量中扣除,配置页提供常见代理名称的一键建议。
- 新增可配置的速率历史时长,默认 30 s。
- 新增编译型 QML 类型 `StateWatcher`,由守护进程的写入唤醒,取代每秒轮询 `cat` 的 `executable` 数据引擎。
- 新增完整 representation 的右键穿透:改用普通 `Item` 根、`KSvg.FrameSvgItem` 标题与裸 `ListView`,并以 `qmltestrunner` 回归测试保证。
- 新增 zh_CN 界面翻译。
- 新增 systemd 服务:仅 `CAP_BPF`、`CAP_PERFMON`、`CAP_SYS_PTRACE` 三项 capability,配合 `ProtectSystem=strict`、`ProtectHome=yes`、`NoNewPrivileges=yes`、capability 边界集与系统调用过滤器。
- 新增 Arch 打包:`packaging/` 下的本地 PKGBUILD 与 AUR 的 `plasma-nethogs-git`。
