#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";


struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, int);
	__type(value, int);
} sched_switch SEC(".maps");


SEC("raw_tp/sched_switch")
int BPF_PROG(handle_sched_switch, bool preempt,
	     struct task_struct *prev, struct task_struct *next,
	     unsigned int prev_state)
{
	pid_t pid;
	int cpu, i = 0;
	int *count;

	pid = BPF_CORE_READ(prev, pid);
	cpu = BPF_CORE_READ(prev, cpu);

	count = bpf_map_lookup_elem(&sched_switch, &cpu);
	if (count)
		i = *count + 1;
	bpf_map_update_elem(&sched_switch, &cpu, &i, BPF_ANY);

	bpf_printk("[%d] Hello world!: %d", cpu, i);
	return 0;
}
