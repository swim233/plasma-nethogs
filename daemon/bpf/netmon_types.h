/* SPDX-License-Identifier: GPL-2.0 */

/* Shared between the BPF program and pnmd. Deliberately free of any include
 * so it can be pulled into both a vmlinux.h world and ordinary C++. */

#pragma once

#define NETMON_COMM_LEN 16

struct netmon_val {
	unsigned long long rx;
	unsigned long long tx;
	char comm[NETMON_COMM_LEN];
};
