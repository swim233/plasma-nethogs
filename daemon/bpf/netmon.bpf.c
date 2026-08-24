// SPDX-License-Identifier: GPL-2.0
//
// Per-process network accounting.
//
// The hook points are asymmetric, and deliberately so.
//
// Receive uses sock_recvmsg(), the one generic entry point every socket read
// funnels through — recv, recvfrom, recvmsg, read on a socket fd, and
// io_uring's equivalents. One hook covers TCP, UDP (and therefore QUIC), IPv4
// and IPv6, and the fexit return value is the number of bytes actually copied
// to userspace.
//
// Send cannot use the mirror image of that. Since Linux 6.7 the syscall path
// calls a static __sock_sendmsg(), which the compiler inlines away; the
// exported sock_sendmsg() survives only for in-kernel callers, so a probe on
// it sees essentially no application traffic. The transport's own sendmsg
// handlers are reached through sk->sk_prot->sendmsg, an indirect call that can
// never be inlined, which makes them stable attach points. tcp_sendmsg serves
// both address families; UDP has one handler per family.
//
// Known gap: splice() from a socket bypasses sock_recvmsg. Modern kernels
// route sendfile() through sendmsg with MSG_SPLICE_PAGES, so the send side is
// covered.

#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "netmon_types.h"

char LICENSE[] SEC("license") = "GPL";

#define AF_INET 2
#define AF_INET6 10

// Keyed by thread group id, i.e. what userspace calls the pid. LRU rather
// than a plain hash so a fork storm evicts cold entries instead of failing
// to insert; pnmd also deletes entries for dead pids on every tick.
struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 8192);
	__type(key, __u32);
	__type(value, struct netmon_val);
} traffic SEC(".maps");

static __always_inline bool is_loopback(struct sock *sk, __u16 family)
{
	if (family == AF_INET) {
		__u32 daddr = bpf_ntohl(BPF_CORE_READ(sk, __sk_common.skc_daddr));
		return (daddr >> 24) == 127;
	}

	__u32 w[4];
	BPF_CORE_READ_INTO(&w, sk, __sk_common.skc_v6_daddr.in6_u.u6_addr32);

	// ::1
	if (w[0] == 0 && w[1] == 0 && w[2] == 0 && w[3] == bpf_htonl(1))
		return true;

	// ::ffff:127.0.0.0/8 (IPv4-mapped loopback)
	if (w[0] == 0 && w[1] == 0 && w[2] == bpf_htonl(0x0000ffff))
		return (bpf_ntohl(w[3]) >> 24) == 127;

	return false;
}

static __always_inline void account(struct sock *sk, long bytes, bool is_rx)
{
	if (bytes <= 0 || !sk)
		return;

	// Drops AF_UNIX, AF_NETLINK and friends. Those dominate a desktop's
	// socket traffic and would otherwise swamp the map.
	__u16 family = BPF_CORE_READ(sk, __sk_common.skc_family);
	if (family != AF_INET && family != AF_INET6)
		return;

	if (is_loopback(sk, family))
		return;

	__u32 tgid = bpf_get_current_pid_tgid() >> 32;

	struct netmon_val *val = bpf_map_lookup_elem(&traffic, &tgid);
	if (val) {
		if (is_rx)
			__sync_fetch_and_add(&val->rx, (__u64)bytes);
		else
			__sync_fetch_and_add(&val->tx, (__u64)bytes);
		return;
	}

	// comm is captured here rather than read from /proc so that a process
	// which exits inside the sampling window still has a readable name.
	struct netmon_val init = {};
	bpf_get_current_comm(&init.comm, sizeof(init.comm));
	if (is_rx)
		init.rx = (__u64)bytes;
	else
		init.tx = (__u64)bytes;

	bpf_map_update_elem(&traffic, &tgid, &init, BPF_NOEXIST);
}

// int tcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
// Shared by IPv4 and IPv6; returns the byte count accepted for transmission.
SEC("fexit/tcp_sendmsg")
int BPF_PROG(fexit_tcp_sendmsg, struct sock *sk, struct msghdr *msg, size_t size, int ret)
{
	account(sk, ret, false);
	return 0;
}

// int udp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
SEC("fexit/udp_sendmsg")
int BPF_PROG(fexit_udp_sendmsg, struct sock *sk, struct msghdr *msg, size_t len, int ret)
{
	account(sk, ret, false);
	return 0;
}

// int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
// Absent when the kernel was built without IPv6, so pnmd treats this one as
// optional at attach time.
SEC("fexit/udpv6_sendmsg")
int BPF_PROG(fexit_udpv6_sendmsg, struct sock *sk, struct msghdr *msg, size_t len, int ret)
{
	account(sk, ret, false);
	return 0;
}

// int sock_recvmsg(struct socket *sock, struct msghdr *msg, int flags)
SEC("fexit/sock_recvmsg")
int BPF_PROG(fexit_sock_recvmsg, struct socket *sock, struct msghdr *msg, int flags, int ret)
{
	account(BPF_CORE_READ(sock, sk), ret, true);
	return 0;
}
