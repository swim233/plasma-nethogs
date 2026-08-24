// SPDX-License-Identifier: GPL-2.0

#include "BpfCollector.h"

#include <QByteArray>
#include <QFile>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// bpftool's generated skeleton casts through char*, which trips -Wcast-align.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#include "plasma-nethogs.skel.h"
#pragma GCC diagnostic pop

#include "plasma-nethogs_types.h"

namespace
{

// libbpf is chatty on stderr by default; route everything but real errors to
// nowhere so the journal stays readable.
int libbpfPrint(enum libbpf_print_level level, const char *format, va_list args)
{
    if (level == LIBBPF_DEBUG) {
        return 0;
    }
    return vfprintf(stderr, format, args);
}

QString errnoString(int err)
{
    return QString::fromLocal8Bit(strerror(err < 0 ? -err : err));
}

} // namespace

BpfCollector::~BpfCollector()
{
    if (m_skel) {
        plasma_nethogs_bpf__destroy(m_skel);
        m_skel = nullptr;
    }
}

bool BpfCollector::load(QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (!QFile::exists(QStringLiteral("/sys/kernel/btf/vmlinux"))) {
        return fail(QStringLiteral(
            "/sys/kernel/btf/vmlinux is missing. plasma-nethogsd needs a kernel built with "
            "CONFIG_DEBUG_INFO_BTF=y."));
    }

    libbpf_set_print(libbpfPrint);

    m_skel = plasma_nethogs_bpf__open();
    if (!m_skel) {
        return fail(QStringLiteral("plasma_nethogs_bpf__open() failed: %1").arg(errnoString(errno)));
    }

    if (const int err = plasma_nethogs_bpf__load(m_skel); err) {
        plasma_nethogs_bpf__destroy(m_skel);
        m_skel = nullptr;
        return fail(QStringLiteral("Loading the BPF object failed: %1. Check that plasma-nethogsd has "
                                   "CAP_BPF and CAP_PERFMON (or runs as root).")
                        .arg(errnoString(err)));
    }

    // Attached one at a time rather than with plasma_nethogs_bpf__attach() so that a
    // failure names the hook, and so that the IPv6 UDP handler can be missing
    // on a kernel built without IPv6 without taking the daemon down.
    const struct {
        bpf_program *program;
        bpf_link **link;
        const char *hook;
        bool required;
    } attachments[] = {
        {m_skel->progs.fexit_tcp_sendmsg, &m_skel->links.fexit_tcp_sendmsg, "tcp_sendmsg", true},
        {m_skel->progs.fexit_udp_sendmsg, &m_skel->links.fexit_udp_sendmsg, "udp_sendmsg", true},
        {m_skel->progs.fexit_udpv6_sendmsg, &m_skel->links.fexit_udpv6_sendmsg, "udpv6_sendmsg", false},
        {m_skel->progs.fexit_sock_recvmsg, &m_skel->links.fexit_sock_recvmsg, "sock_recvmsg", true},
    };

    for (const auto &attachment : attachments) {
        bpf_link *link = bpf_program__attach(attachment.program);
        const int err = libbpf_get_error(link);
        if (err) {
            if (!attachment.required) {
                continue;
            }
            plasma_nethogs_bpf__destroy(m_skel);
            m_skel = nullptr;
            return fail(QStringLiteral("Attaching fexit/%1 failed: %2. Confirm the kernel exports "
                                       "it with `bpftool btf dump file /sys/kernel/btf/vmlinux "
                                       "format raw | grep \"FUNC '%1'\"`.")
                            .arg(QLatin1StringView(attachment.hook), errnoString(err)));
        }
        *attachment.link = link;
    }

    m_mapFd = bpf_map__fd(m_skel->maps.traffic);
    if (m_mapFd < 0) {
        plasma_nethogs_bpf__destroy(m_skel);
        m_skel = nullptr;
        return fail(QStringLiteral("The traffic map has no file descriptor."));
    }

    return true;
}

QList<RawSample> BpfCollector::poll() const
{
    QList<RawSample> samples;
    if (m_mapFd < 0) {
        return samples;
    }

    quint32 key = 0;
    quint32 nextKey = 0;
    bool haveKey = false;

    while (bpf_map_get_next_key(m_mapFd, haveKey ? &key : nullptr, &nextKey) == 0) {
        plasma_nethogs_val value{};
        if (bpf_map_lookup_elem(m_mapFd, &nextKey, &value) == 0) {
            RawSample sample;
            sample.tgid = nextKey;
            sample.rx = value.rx;
            sample.tx = value.tx;
            // comm is not guaranteed NUL-terminated when it fills the array.
            sample.comm = QString::fromLocal8Bit(
                value.comm, qstrnlen(value.comm, sizeof(value.comm)));
            samples.append(sample);
        }
        key = nextKey;
        haveKey = true;
    }

    return samples;
}

void BpfCollector::forget(quint32 tgid) const
{
    if (m_mapFd >= 0) {
        bpf_map_delete_elem(m_mapFd, &tgid);
    }
}
