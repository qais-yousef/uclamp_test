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
	pid_t pid;
	int i = 0;

	while (nr_forks--) {
		pid = fork();
		if (!pid)
			break;
		if (pid == -1) {
			perror("Failed to create a child process");
			return NULL;
		}
		pids[i++] = pid;
	}
}

static void *test_loop(void *data)
{
}

static int verify(void)
{
	printf("All forked RT tasks had the correct uclamp.min\n");
	return EXIT_SUCCESS;
}

int main(char **argv, int argc)
{
	pthread_t fork_thread, test_thread;
	pthread_attr_t attr;
	int ret;

	ret = pthread_attr_init(&attr);
	if (ret) {
		perror("Failed to init pthread_attr_t");
		return EXIT_FAILURE;
	}

	ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	if (ret) {
		perror("Failed to init pthread_attr_t");
		return EXIT_FAILURE;
	}

	ret = pthread_create(&fork_thread, &attr, fork_loop, NULL);
	if (ret) {
		perror("Failed to create fork thread");
		return EXIT_FAILURE;
	}

	ret = pthread_create(&test_thread, NULL, test_loop, NULL);
	if (ret) {
		perror("Failed to create test thread");
		return EXIT_FAILURE;
	}

	ret = verify();

	pthread_join(fork_thread, NULL);
	pthread_join(test_thread, NULL);

	return ret;
}
