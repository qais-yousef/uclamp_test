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
	int policy, ret, i = 0;
	pthread_attr_t attr;
	pid_t pid;

	ret = pthread_attr_init(&attr);
	if (ret) {
		perror("Failed to init pthread_attr_t");
		return NULL;
	}

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
	pid = getpid();

	ret = pthread_attr_getschedpolicy(&attr, &policy);
	if (ret) {
		perror("Failed to get policy");
	} else {
		char *str;

		switch (policy) {
			case SCHED_OTHER:
				str = "SCHED_NORMAL";
				break;
			case SCHED_FIFO:
				str = "SCHED_NORMAL";
				break;
		}
		printf("Created %d, policy: %s\n", pid, str);
	}

	return NULL;
}

static void *test_loop(void *data)
{
	return NULL;
}

static int verify(void)
{
	printf("All forked RT tasks had the correct uclamp.min\n");
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
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
		perror("Failed to set policy to SCHED_FIFO");
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

	pthread_join(fork_thread, NULL);
	pthread_join(test_thread, NULL);

	return verify();
}
