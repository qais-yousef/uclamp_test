/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Qais Yousef */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "uclamp_test_thermal_pressure_events.h"

char LICENSE[] SEC("license") = "GPL";


#define TASK_COMM_LEN	16
#define PELT_TYPE_LEN	4
#define RB_SIZE		(256 * 1024)

pid_t pid = 0;

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, RB_SIZE);
} rq_pelt_rb SEC(".maps");


SEC("kprobe/enqueue_task_fair")
int BPF_KPROBE(kprobe_enqueue_task_fair, struct rq *rq, struct task_struct *p)
{
	struct rq_pelt_event *e;

	pid_t ppid = BPF_CORE_READ(p, pid);
	int cpu = BPF_CORE_READ(rq, cpu);

	unsigned long util_avg = BPF_CORE_READ(rq, cfs.avg.util_avg);
	unsigned long thermal_avg = BPF_CORE_READ(rq, avg_thermal.util_avg);
	unsigned long capacity_orig = BPF_CORE_READ(rq, cpu_capacity_orig);
	unsigned long uclamp_min = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MIN].value);
	unsigned long uclamp_max = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MAX].value);

	if (!pid || pid != ppid)
		return 0;

	e = bpf_ringbuf_reserve(&rq_pelt_rb, sizeof(*e), 0);
	if (e) {
		e->ts = bpf_ktime_get_ns();
		e->cpu = cpu;
		e->util_avg = util_avg;
		e->thermal_avg = thermal_avg;
		e->capacity_orig = capacity_orig;
		e->uclamp_min = uclamp_min;
		e->uclamp_max = uclamp_max;
		bpf_ringbuf_submit(e, 0);
	}
	return 0;
}
