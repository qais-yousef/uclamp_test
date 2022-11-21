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


/* Maps */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct compute_energy_event);
} compute_energy_map SEC(".maps");

/* Ring Buffers */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, RB_SIZE);
} rq_pelt_rb SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, RB_SIZE);
} compute_energy_rb SEC(".maps");


SEC("kprobe/enqueue_task_fair")
int BPF_KPROBE(kprobe_enqueue_task_fair, struct rq *rq, struct task_struct *p)
{
	struct rq_pelt_event *e;

	pid_t ppid = BPF_CORE_READ(p, pid);
	int cpu = BPF_CORE_READ(rq, cpu);

	unsigned long rq_util_avg = BPF_CORE_READ(rq, cfs.avg.util_avg);
	unsigned long p_util_avg = BPF_CORE_READ(p, se.avg.util_avg);
	unsigned long thermal_avg = BPF_CORE_READ(rq, avg_thermal.util_avg);
	unsigned long capacity_orig = BPF_CORE_READ(rq, cpu_capacity_orig);
	unsigned long uclamp_min = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MIN].value);
	unsigned long uclamp_max = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MAX].value);
	int overutilized = BPF_CORE_READ(rq, rd, overutilized);

	if (!pid || pid != ppid)
		return 0;

	e = bpf_ringbuf_reserve(&rq_pelt_rb, sizeof(*e), 0);
	if (e) {
		e->ts = bpf_ktime_get_ns();
		e->cpu = cpu;
		e->rq_util_avg = rq_util_avg;
		e->p_util_avg = p_util_avg;
		e->thermal_avg = thermal_avg;
		e->capacity_orig = capacity_orig;
		e->uclamp_min = uclamp_min;
		e->uclamp_max = uclamp_max;
		e->overutilized = overutilized;
		bpf_ringbuf_submit(e, 0);
	}
	return 0;
}

SEC("kprobe/compute_energy")
int BPF_KPROBE(kprobe_compute_energy, struct task_struct *p, int dst_cpu)
{
	struct compute_energy_event *e;
	int cpu = bpf_get_smp_processor_id();
	pid_t ppid = BPF_CORE_READ(p, pid);

	unsigned long p_util_avg = BPF_CORE_READ(p, se.avg.util_avg);
	unsigned long uclamp_min = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MIN].value);
	unsigned long uclamp_max = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MAX].value);

	if (!pid || pid != ppid)
		return 0;

	e = bpf_ringbuf_reserve(&compute_energy_rb, sizeof(*e), 0);
	if (e) {
		e->ts = bpf_ktime_get_ns();
		e->dst_cpu = dst_cpu;
		e->p_util_avg = p_util_avg;
		e->uclamp_min = uclamp_min;
		e->uclamp_max = uclamp_max;

		bpf_map_update_elem(&compute_energy_map, &cpu, e, BPF_ANY);
	}
	return 0;
}

SEC("kretprobe/compute_energy")
int BPF_KRETPROBE(kretprobe_compute_energy)
{
	struct compute_energy_event *e;
	int cpu = bpf_get_smp_processor_id();
	int ret = PT_REGS_RC(ctx);

	e = bpf_map_lookup_elem(&compute_energy_map, &cpu);
	if (!e)
		return 0;

	e->energy = ret;
	bpf_ringbuf_submit(e, 0);
	return 0;
}
