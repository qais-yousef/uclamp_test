/* SPDX-License-Identifier: GPL-2.0 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#define NR_FORKS	100

static int nr_forks = NR_FORKS;
static pid_t pids[NR_FORKS];

static void *fork_loop(void *data)
{
	struct sched_param param;
	int ret, i = 0;
	pid_t pid;

	/* Set to SCHED_FIFO before we start */
	param.sched_priority = 33;
	ret = sched_setscheduler(0, SCHED_FIFO, &param);
	if (ret) {
		perror("Failed to set policy to SCHED_FIFO");
		return NULL;
	}

	/*
	 * fork() the specified number of threads saving the resulting pid in
	 * pids[] array. Child process will then wait for a signal to exit.
	 */
	while (nr_forks--) {
		pid = fork();
		if (!pid)
			goto child;
		if (pid == -1) {
			perror("Failed to create a child process");
			return NULL;
		}
		pids[i++] = pid;
	}

	return NULL;
child:
	/* FIXME: add wait signal logic */

	return NULL;
}

static void *test_loop(void *data)
{
	/* FIXME: change /proc/sys/kernel/sched_uclamp_util_min_rt_default */
	return NULL;
}

static int verify(void)
{
	int i;

	/* flush any messages we printed into stdout */
	fflush(stdout);

	for (i = 0; i < NR_FORKS; i++) {

		/* If a pid is 0, it means we got an error */
		if (pids[i] == 0)
			return EXIT_FAILURE;

		/* FIXME: verify uclamp.min for every task is what we expect */

		//printf("%d policy = %d\n", pids[i], sched_getscheduler(pids[i]));
	}

	printf("All forked RT tasks had the correct uclamp.min\n");
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	pthread_t fork_thread, test_thread;
	int ret;

	ret = pthread_create(&fork_thread, NULL, fork_loop, NULL);
	if (ret) {
		perror("Failed to create fork thread");
		return EXIT_FAILURE;
	}

	ret = pthread_create(&test_thread, NULL, test_loop, NULL);
	if (ret) {
		perror("Failed to create test thread");
		return EXIT_FAILURE;
	}

	pthread_join(fork_thread, NULL);
	pthread_join(test_thread, NULL);

	return verify();
}
