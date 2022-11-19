/* SPDX-License-Identifier: GPL-2.0 */
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


#define NR_LOOPS	100000

static bool volatile start = false;
static bool volatile done = false;


static int handle_rq_pelt_event(void *ctx, void *data, size_t data_sz)
{
	struct rq_pelt_event *e = data;
	/* static FILE *file = NULL; */

	/* if (!file) { */
	/* 	file = fopen("uclamp_test_thermal_pressure.csv", "w"); */
	/* 	if (!file) */
	/* 		return 0; */
	/* 	fprintf(file, "ts,cpu,util, capacity_orig, thermal_avg, uclamp_min,uclamp_max\n"); */
	/* } */

	fprintf(stdout, "%llu,%d,%lu, %lu, %lu, %lu,%lu\n",
		e->ts,e->cpu, e->util_avg, e->capacity_orig, e->thermal_avg, e->uclamp_min, e->uclamp_max);

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
	int loops = NR_LOOPS;

	skel->bss->pid = gettid();

	while (!start)
		usleep(5000);

	while (!done) {

		if (!loops--) {
			loops = NR_LOOPS;
			usleep(16000);
		}
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

	start = true;
	sleep(10);
	pr_debug("pid: %u\n", skel->bss->pid);

cleanup:
	if (!start)
		start = true;
	done = true;
	pthread_join(thread, NULL);

	pr_debug("main pid: %u\n", gettid());

	DESTROY_EVENT_THREAD(rq_pelt);
	uclamp_test_thermal_pressure_bpf__destroy(skel);
	return ret < 0 ? -ret : EXIT_SUCCESS;
}
