/* SPDX-License-Identifier: GPL-2.0 */

/* Shared between the BPF program and plasma-nethogsd. Deliberately free of any include
 * so it can be pulled into both a vmlinux.h world and ordinary C++. */

#pragma once

#define PLASMA_NETHOGS_COMM_LEN 16

struct plasma_nethogs_val {
	unsigned long long rx;
	unsigned long long tx;
	char comm[PLASMA_NETHOGS_COMM_LEN];
};
