/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Qais Yousef */
#include "events_defs.h"
#include "sched.h"

#include <bpf/libbpf.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
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

#define PELT_CSV_FILE	"uclamp_test_thermal_pressure_pelt.csv"
static int handle_rq_pelt_event(void *ctx, void *data, size_t data_sz)
{
	struct rq_pelt_event *e = data;
	static FILE *file = NULL;
	static bool err_once = false;
	unsigned long capacity_thermal, cap;
	int i;

	capacity_thermal = e->capacity_orig - e->thermal_avg;

	if (!file) {
		file = fopen(PELT_CSV_FILE, "w");
		if (!file) {
			if (!err_once) {
				err_once = true;
				fprintf(stderr, "Failed to create %s file\n", PELT_CSV_FILE);
			}
			return 0;
		}
		fprintf(stdout, "Created %s\n", PELT_CSV_FILE);
		fprintf(file, "ts, cpu, rq_util, p_util, capacity_orig, thermal_avg, uclamp_min, uclamp_max, overutilized\n");
	}

	if (e->uclamp_min > e->capacity_orig)
		fprintf(stderr, "[%llu] Failed: uclamp_min > capacity_orig --::-- %lu > %lu\n", e->ts, e->uclamp_min, e->capacity_orig);

	if (e->p_util_avg > e->uclamp_max && e->rq_util_avg == e->uclamp_max &&  e->uclamp_max < e->capacity_orig)
		fprintf(stderr, "[%llu] Warning: uclamp_max < capacity_orig --::-- %lu < %lu\n", e->ts, e->uclamp_max, e->capacity_orig);

#ifdef VERBOSE
	if (e->p_util_avg > e->uclamp_max && e->rq_util_avg > e->uclamp_max)
		fprintf(stderr, "[%llu] Warning: rq_util_avg > uclamp_max --::-- %lu > %lu\n", e->ts, e->rq_util_avg, e->uclamp_max);
#endif

	if (e->capacity_orig != 1024 && e->uclamp_min > capacity_thermal) {
		fprintf(stderr, "[%llu] Failed: uclamp_min > capacity_orig - thermal_avg --::-- %lu > %lu - %lu (%lu)\n",
			e->ts, e->uclamp_min, e->capacity_orig, e->thermal_avg, capacity_thermal);
	}

	for_each_capacity(cap, i) {
#ifdef VERBOSE
		if (e->uclamp_min <= cap && e->capacity_orig > cap)
			fprintf(stderr, "[%llu] Warning: uclamp_min = %lu --::-- running on %lu instead of %lu\n", e->ts, e->uclamp_min, e->capacity_orig, cap);
#endif

		if (cap < e->capacity_orig && capacity_thermal < cap) {
			fprintf(stderr, "[%llu] Warning: capacity_inversion --::-- capacity_orig - thermal_avg < cap --::-- %lu - %lu (%lu) < %lu\n",
				e->ts, e->capacity_orig, e->thermal_avg, capacity_thermal, cap);
		}
	}

	fprintf(file, "%llu, %d, %lu, %lu, %lu, %lu, %lu,%lu, %d\n",
		e->ts, e->cpu, e->rq_util_avg, e->p_util_avg, e->capacity_orig, e->thermal_avg, e->uclamp_min, e->uclamp_max, e->overutilized);

	fflush(file);
	return 0;
}

#define STRQF_CSV_FILE	"uclamp_test_thermal_pressure_strqf.csv"
static int handle_select_task_rq_fair_event(void *ctx, void *data, size_t data_sz)
{
	struct select_task_rq_fair_event *e = data;
	static FILE *file = NULL;
	static bool err_once = false;

	if (!file) {
		file = fopen(STRQF_CSV_FILE, "w");
		if (!file) {
			if (!err_once) {
				err_once = true;
				fprintf(stderr, "Failed to create %s file\n", STRQF_CSV_FILE);
			}
			return 0;
		}
		fprintf(stdout, "Created %s\n", STRQF_CSV_FILE);
		fprintf(file, "ts, cpu, p_util, uclamp_min, uclamp_max\n");
	}

	fprintf(file, "%llu, %d, %lu, %lu,%lu\n",
		e->ts, e->cpu, e->p_util_avg, e->uclamp_min, e->uclamp_max);

	fflush(file);
	return 0;
}

#define COMPUTE_ENERGY_CSV_FILE	"uclamp_test_thermal_pressure_compute_energy.csv"
static int handle_compute_energy_event(void *ctx, void *data, size_t data_sz)
{
	struct compute_energy_event *e = data;
	static FILE *file = NULL;
	static bool err_once = false;

	if (!file) {
		file = fopen(COMPUTE_ENERGY_CSV_FILE, "w");
		if (!file) {
			if (!err_once) {
				err_once = true;
				fprintf(stderr, "Failed to create %s file\n", COMPUTE_ENERGY_CSV_FILE);
			}
			return 0;
		}
		fprintf(stdout, "Created %s\n", COMPUTE_ENERGY_CSV_FILE);
		fprintf(file, "ts, dst_cpu, p_util, uclamp_min, uclamp_max, energy\n");
	}

	fprintf(file, "%llu, %d, %lu, %lu, %lu, %lu\n",
		e->ts, e->dst_cpu, e->p_util_avg, e->uclamp_min, e->uclamp_max, e->energy);

	fflush(file);
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
EVENT_THREAD_FN(select_task_rq_fair)
EVENT_THREAD_FN(compute_energy)

static inline __attribute__((always_inline)) void do_light_work(void)
{
	int loops = NR_LOOPS;

	while (loops--)
		usleep(16000);
}

static inline __attribute__((always_inline)) void do_busy_work(void)
{
	struct timespec start, ts;
	long int time_diff_us;
	int loops = NR_LOOPS;
	int result, ret;

	while (loops--) {
		ret = clock_gettime(CLOCK_MONOTONIC_RAW, &start);
		if (ret) {
			perror("Failed to get time");
			return;
		}
		while (true) {
			result = pow(loops, loops);
			result = sqrt(result);

			ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
			if (ret) {
				perror("Failed to get time");
				return;
			}

			time_diff_us = (ts.tv_sec - start.tv_sec) * 1000000;
			time_diff_us += (ts.tv_nsec - start.tv_nsec) / 1000;

			pr_debug("ts.tv_sec: %ld ts.tv_nsec: %ld\n", ts.tv_sec, ts.tv_nsec);
			pr_debug("start.tv_sec: %ld start.tv_nsec: %ld\n", start.tv_sec, start.tv_nsec);
			pr_debug("time_diff: %ld\n", time_diff_us);

			if (time_diff_us >= 14000)
				break;
		}
		usleep(2000);
	}
}

static void print_uclamp_values(void)
{
	struct sched_attr sched_attr;
	pid_t pid = gettid();
	int ret;

	ret = sched_getattr(pid, &sched_attr, sizeof(struct sched_attr), 0);
	if (ret) {
		perror("Failed to get attr");
		fprintf(stderr, "Couldn't get schedattr for pid %d\n", pid);
		return;
	}

	fprintf(stdout, "Getting uclamp_min: %u uclamp_max: %u\n",
		sched_attr.sched_util_min, sched_attr.sched_util_max);
}

static int set_uclamp_values(struct sched_attr *sched_attr,
			     unsigned long uclamp_min, unsigned long uclamp_max)
{
	pid_t pid = gettid();
	int ret;

	fprintf(stdout, "Setting uclamp_min: %lu uclamp_max: %lu\n", uclamp_min, uclamp_max);
	sched_attr->sched_util_min = uclamp_min;
	sched_attr->sched_util_max = uclamp_max;
	sched_attr->sched_flags = SCHED_FLAG_KEEP_ALL | SCHED_FLAG_UTIL_CLAMP;
	ret = sched_setattr(pid, sched_attr, 0);
	if (ret) {
		perror("Failed to set attr");
		fprintf(stderr, "Couldn't set schedattr for pid %d\n", pid);
		return ret;
	}

	print_uclamp_values();

	usleep(1000);

	return 0;
}

static int test_uclamp_min(void)
{
	struct sched_attr sched_attr;
	pid_t pid = gettid();
	unsigned long cap;
	int ret, i;

	ret = sched_getattr(pid, &sched_attr, sizeof(struct sched_attr), 0);
	if (ret) {
		perror("Failed to get attr");
		fprintf(stderr, "Couldn't get schedattr for pid %d\n", pid);
		return ret;
	}

	fprintf(stdout, "::-- Testing uclamp_min --::\n");

	/* Run first with default values */
	ret = set_uclamp_values(&sched_attr, 0, 1024);
	if (ret)
		return ret;
	do_light_work();

	/* Run at capacity boundaries */
	for_each_capacity(cap, i) {
		ret = set_uclamp_values(&sched_attr, 0, 1024);
		if (ret)
			return ret;
		do_light_work();

		ret = set_uclamp_values(&sched_attr, cap, 1024);
		if (ret)
			return ret;
		do_light_work();
	}

	/* Run at capacity boundaries + 1 */
	for_each_capacity(cap, i) {
		ret = set_uclamp_values(&sched_attr, 0, 1024);
		if (ret)
			return ret;
		do_light_work();

		cap = cap == 1024 ? 1024 : cap + 1;
		ret = set_uclamp_values(&sched_attr, cap, 1024);
		if (ret)
			return ret;
		do_light_work();
	}

	return 0;
}

static int test_uclamp_max(void)
{
	struct sched_attr sched_attr;
	pid_t pid = gettid();
	unsigned long cap;
	int ret, i;

	ret = sched_getattr(pid, &sched_attr, sizeof(struct sched_attr), 0);
	if (ret) {
		perror("Failed to get attr");
		fprintf(stderr, "Couldn't get schedattr for pid %d\n", pid);
		return ret;
	}

	fprintf(stdout, "::-- Testing uclamp_max --::\n");

	/* Run first with 0 */
	ret = set_uclamp_values(&sched_attr, 0, 0);
	if (ret)
		return ret;
	do_light_work();
	do_busy_work();

	/* Run at capacity boundaries */
	for_each_capacity(cap, i) {
		ret = set_uclamp_values(&sched_attr, 0, cap);
		if (ret)
			return ret;
		do_light_work();
		do_busy_work();
	}

	/* Run at capacity boundaries + 1 */
	for_each_capacity(cap, i) {
		cap = cap == 1024? 1023 : cap + 1;
		ret = set_uclamp_values(&sched_attr, 0, cap);
		if (ret)
			return ret;
		do_light_work();
		do_busy_work();
	}

	return 0;
}

static void *thread_loop(void *data)
{
	pid_t pid = gettid();
	int ret;

	skel->bss->pid = pid;

	ret = get_capacities();
	if (ret)
		return NULL;

	while (!start)
		usleep(5000);

	ret = test_uclamp_min();
	if (ret)
		return NULL;

	ret = test_uclamp_max();
	if (ret)
		return NULL;

	pr_debug("thread_loop pid: %u\n", pid);

	return NULL;
}

int main(int argc, char **argv)
{
	INIT_EVENT_THREAD(rq_pelt);
	INIT_EVENT_THREAD(select_task_rq_fair);
	INIT_EVENT_THREAD(compute_energy);
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
	CREATE_EVENT_THREAD(select_task_rq_fair);
	CREATE_EVENT_THREAD(compute_energy);

	/* Wait for events threads to start */
	sleep(1);

cleanup:
	start = true;
	pthread_join(thread, NULL);
	done = true;

	pr_debug("main pid: %u\n", gettid());

	DESTROY_EVENT_THREAD(rq_pelt);
	DESTROY_EVENT_THREAD(select_task_rq_fair);
	DESTROY_EVENT_THREAD(compute_energy);
	uclamp_test_thermal_pressure_bpf__destroy(skel);
	return ret < 0 ? -ret : EXIT_SUCCESS;
}
