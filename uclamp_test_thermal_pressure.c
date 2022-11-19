/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Qais Yousef */
#include "events_defs.h"
#include "sched.h"

#include <bpf/libbpf.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "uclamp_test_thermal_pressure.skel.h"
#include "uclamp_test_thermal_pressure_events.h"

//#define DEBUG
#ifdef DEBUG
#define pr_debug	printf
#else
#define pr_debug(...)
#endif


#define NR_LOOPS	100

static bool volatile start = false;
static bool volatile done = false;

struct capacities {
	unsigned long *cap;
	unsigned int len;
} capacities;

#define for_each_capacity(cap, i)	\
	for ((i) = 0, (cap) = capacities.cap[(i)]; (i) < capacities.len; (i)+=1, (cap) = capacities.cap[(i)])

static inline __attribute__((always_inline)) void do_work() {
	int loops = NR_LOOPS;

	while (loops--)
		usleep(16000);
}

#define CSV_FILE	"uclamp_test_thermal_pressure.csv"
static int handle_rq_pelt_event(void *ctx, void *data, size_t data_sz)
{
	struct rq_pelt_event *e = data;
	static FILE *file = NULL;
	static bool err_once = false;

	if (!file) {
		file = fopen(CSV_FILE, "w");
		if (!file) {
			if (!err_once) {
				err_once = true;
				fprintf(stderr, "Failed to create %s file\n", CSV_FILE);
			}
			return 0;
		}
		fprintf(stdout, "Created %s\n", CSV_FILE);
		fprintf(file, "ts,cpu,util, capacity_orig, thermal_avg, uclamp_min,uclamp_max, overutilized\n");
	}

	fprintf(file, "%llu,%d,%lu, %lu, %lu, %lu,%lu, %d\n",
		e->ts,e->cpu, e->util_avg, e->capacity_orig, e->thermal_avg, e->uclamp_min, e->uclamp_max, e->overutilized);

	return 0;
}

#define SYSFS_CAPACITIES	"/sys/devices/system/cpu/cpu*/cpu_capacity"
static int get_capacities(void)
{
	long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	char str[256] = {};
	char *cap;
	int read;

	FILE *fp = popen("cat " SYSFS_CAPACITIES " | sort -un", "r");
	if (!fp) {
		perror("Can't read capacities");
		return -1;
	}

	read = fread(str, 1, 256, fp);
	fclose(fp);
	if (!read) {
		perror("Failed to read capacities");
		return -1;
	}

	capacities.cap = calloc(num_cpus, sizeof(unsigned long));
	cap = strtok(str, "\n");
	while (cap) {
		pr_debug("Adding capacity: %i\n", atoi(cap));
		capacities.cap[capacities.len] = atoi(cap);
		capacities.len++;
		cap = strtok(NULL, "\n");
	}

	return 0;
}

/*
 * All events require to access this variable to get access to the ringbuffer.
 * Make it available for all event##_thread_fn.
 */
struct uclamp_test_thermal_pressure_bpf *skel;

/*
 * Define a pthread function handler for each event
 */
EVENT_THREAD_FN(rq_pelt)

static void *thread_loop(void *data)
{
	struct sched_attr sched_attr;
	pid_t pid = gettid();
	unsigned long cap;
	int ret, i;

	skel->bss->pid = pid;

	ret = sched_getattr(pid, &sched_attr, sizeof(struct sched_attr), 0);
	if (ret) {
		perror("Failed to get attr");
		fprintf(stderr, "Couldn't get schedattr for pid %d\n", pid);
		return NULL;
	}

	ret = get_capacities();
	if (ret)
		return NULL;

	while (!start)
		usleep(5000);

	/* Run first with default values */
	do_work();

	for_each_capacity(cap, i) {

		pr_debug("Setting capacity: %lu\n", cap);
		sched_attr.sched_util_min = cap;
		sched_attr.sched_flags = SCHED_FLAG_KEEP_ALL | SCHED_FLAG_UTIL_CLAMP;
		ret = sched_setattr(pid, &sched_attr, 0);
		if (ret) {
			perror("Failed to set attr");
			fprintf(stderr, "Couldn't set schedattr for pid %d\n", pid);
			return NULL;
		}
		do_work();
	}

	pr_debug("thread_loop pid: %u\n", gettid());

	return NULL;
}

int main(int argc, char **argv)
{
	INIT_EVENT_THREAD(rq_pelt);
	pthread_t thread;
	int ret;

	skel = uclamp_test_thermal_pressure_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open and load BPF skeleton\n");
		return EXIT_FAILURE;
	}

	ret = pthread_create(&thread, NULL, thread_loop, NULL);
	if (ret) {
		perror("Failed to create thread");
		goto cleanup;
	}

	ret = uclamp_test_thermal_pressure_bpf__load(skel);
	if (ret) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}

	ret = uclamp_test_thermal_pressure_bpf__attach(skel);
	if (ret) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

	CREATE_EVENT_THREAD(rq_pelt);

cleanup:
	start = true;
	pthread_join(thread, NULL);
	done = true;

	pr_debug("main pid: %u\n", gettid());

	DESTROY_EVENT_THREAD(rq_pelt);
	uclamp_test_thermal_pressure_bpf__destroy(skel);
	return ret < 0 ? -ret : EXIT_SUCCESS;
}
