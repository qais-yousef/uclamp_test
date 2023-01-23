/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Qais Yousef */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "uclamp_test_thermal_pressure_events.h"

char LICENSE[] SEC("license") = "GPL";


/* SCHED defines */
#define TASK_COMM_LEN	16
#define ENQUEUE_WAKEUP  0x01

#define PELT_TYPE_LEN	4
#define RB_SIZE		(256 * 1024)

/* Global public variables shared with userspace*/
pid_t pid = 0;

/* Global private variables */
struct task_struct *task = NULL;
struct rq *etf_rq = NULL;
struct task_struct *etf_p = NULL;


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
} select_task_rq_fair_rb SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, RB_SIZE);
} compute_energy_rb SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, RB_SIZE);
} overutilized_rb SEC(".maps");


SEC("kprobe/enqueue_task_fair")
int BPF_KPROBE(kprobe_enqueue_task_fair, struct rq *rq, struct task_struct *p,
	       int flags)
{
	/* We only cared about enqueues at wake up */
	if (!(flags & ENQUEUE_WAKEUP))
		return 0;

	pid_t ppid = BPF_CORE_READ(p, pid);

	if (!pid || pid != ppid)
		return 0;

	etf_rq = rq;
	etf_p = p;

	return 0;
}

SEC("kretprobe/enqueue_task_fair")
int BPF_KRETPROBE(kretprobe_enqueue_task_fair)
{
	struct rq_pelt_event *e;
	struct rq *rq = etf_rq;
	struct task_struct *p = etf_p;

	if (!rq || !p)
		return 0;

	pid_t ppid = BPF_CORE_READ(p, pid);

	if (!pid || pid != ppid)
		return 0;

	etf_rq = NULL;
	etf_p = NULL;

	int cpu = BPF_CORE_READ(rq, cpu);

	unsigned long rq_util_avg = BPF_CORE_READ(rq, cfs.avg.util_avg);
	unsigned long p_util_avg = BPF_CORE_READ(p, se.avg.util_avg);
	unsigned long thermal_avg = BPF_CORE_READ(rq, avg_thermal.util_avg);
	unsigned long capacity_orig = BPF_CORE_READ(rq, cpu_capacity_orig);
	unsigned long uclamp_min = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MIN].value);
	unsigned long uclamp_max = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MAX].value);
	int overutilized = BPF_CORE_READ(rq, rd, overutilized);

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

SEC("kprobe/select_task_rq_fair")
int BPF_KPROBE(kprobe_select_task_rq_fair, struct task_struct *p)
{
	pid_t ppid = BPF_CORE_READ(p, pid);

	if (!pid || pid != ppid)
		return 0;

	task = p;

	return 0;
}

SEC("kretprobe/select_task_rq_fair")
int BPF_KRETPROBE(kretprobe_select_task_rq_fair)
{
	int cpu = PT_REGS_RC(ctx);
	struct select_task_rq_fair_event *e;
	struct task_struct *p = task;

	if (!p)
		return 0;

	pid_t ppid = BPF_CORE_READ(p, pid);

	if (!pid || pid != ppid)
		return 0;

	task = NULL;

	unsigned long p_util_avg = BPF_CORE_READ(p, se.avg.util_avg);
	unsigned long uclamp_min = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MIN].value);
	unsigned long uclamp_max = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MAX].value);

	e = bpf_ringbuf_reserve(&select_task_rq_fair_rb, sizeof(*e), 0);
	if (e) {
		e->ts = bpf_ktime_get_ns();
		e->cpu = cpu;
		e->p_util_avg = p_util_avg;
		e->uclamp_min = uclamp_min;
		e->uclamp_max = uclamp_max;
		bpf_ringbuf_submit(e, 0);
	}
	return 0;
}

SEC("raw_tp/sched_overutilized_tp")
int BPF_PROG(handle_overutilized_tp, struct root_domain *rd, int overutilized)
{
	struct overutilized_event *e;

	e = bpf_ringbuf_reserve(&overutilized_rb, sizeof(*e), 0);
	if (e) {
		e->ts = bpf_ktime_get_ns();
		e->overutilized = overutilized;
		bpf_ringbuf_submit(e, 0);
	}

	return 0;
}

SEC("raw_tp/sched_compute_energy_tp")
int BPF_PROG(handle_compute_energy, struct task_struct *p,
	     int dst_cpu, unsigned long energy)
{
	struct compute_energy_event *e;
	pid_t ppid = BPF_CORE_READ(p, pid);

	if (dst_cpu == -1)
		return 0;

	if (!pid || pid != ppid)
		return 0;

	unsigned long p_util_avg = BPF_CORE_READ(p, se.avg.util_avg);
	unsigned long uclamp_min = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MIN].value);
	unsigned long uclamp_max = BPF_CORE_READ_BITFIELD_PROBED(p, uclamp[UCLAMP_MAX].value);

	e = bpf_ringbuf_reserve(&compute_energy_rb, sizeof(*e), 0);
	if (e) {
		e->ts = bpf_ktime_get_ns();
		e->dst_cpu = dst_cpu;
		e->p_util_avg = p_util_avg;
		e->uclamp_min = uclamp_min;
		e->uclamp_max = uclamp_max;
		e->energy = energy;
		bpf_ringbuf_submit(e, 0);
	}

	return 0;
}
