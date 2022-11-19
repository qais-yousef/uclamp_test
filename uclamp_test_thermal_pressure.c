/* SPDX-License-Identifier: GPL-2.0 */
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

//#define DEBUG
#ifdef DEBUG
#define pr_debug	printf
#else
#define pr_debug(...)
#endif


#define NR_LOOPS	1000000

static bool volatile start = false;
static bool volatile done = false;


static void *thread_loop(void *data)
{
	struct uclamp_test_thermal_pressure_bpf *skel = data;
	volatile int loops = NR_LOOPS;

	skel->bss->pid = getpid();

	while (!start)
		usleep(10000);

	while (!done) {

		while (loops--);

		loops = NR_LOOPS;

		usleep(500);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	struct uclamp_test_thermal_pressure_bpf *skel;
	pthread_t thread;
	int ret;

	skel = uclamp_test_thermal_pressure_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open and load BPF skeleton\n");
		return EXIT_FAILURE;
	}

	ret = pthread_create(&thread, NULL, thread_loop, skel);
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

	start = true;
	sleep(5);
	printf("pid: %u\n", skel->bss->pid);

cleanup:
	if (!start)
		start = true;
	done = true;
	pthread_join(thread, NULL);

	uclamp_test_thermal_pressure_bpf__destroy(skel);
	return ret < 0 ? -ret : EXIT_SUCCESS;
}
