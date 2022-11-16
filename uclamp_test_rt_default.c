/* SPDX-License-Identifier: GPL-2.0 */
#include "sched.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NR_FORKS	10000

static int nr_forks = NR_FORKS;
static pid_t pids[NR_FORKS];

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

//#define DEBUG
#ifdef DEBUG
#define pr_debug	printf
#else
#define pr_debug(...)
#endif

#define PROCFS_RT_MIN	"/proc/sys/kernel/sched_util_clamp_min_rt_default"
static int orig_rt_min;
static int test_rt_min = 333;

static int read_rt_min(void)
{
	char str[16] = {};

	FILE *fp = fopen(PROCFS_RT_MIN, "r");
	if (!fp) {
		perror("Can't open procfs");
		return -1;
	}

	fread(str, 1, 16, fp);
	fclose(fp);

	pr_debug("read_rt_min = %s\n", str);

	return atoi(str);
}

static void write_rt_min(int value)
{
	char str[16] = {};

	FILE *fp = fopen(PROCFS_RT_MIN, "w");
	if (!fp) {
		perror("Can't open procfs");
		return;
	}

	snprintf(str, 16, "%d", value);
	fwrite(str, 1, strlen(str), fp);
	fclose(fp);

	pr_debug("write_rt_min = %s\n", str);
}

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

		usleep(500);
	}

	return NULL;
child:
	pthread_mutex_lock(&mutex);
	pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);

	return NULL;
}

static int verify_pid(pid_t pid)
{
	struct sched_attr sched_attr;
	int ret;

	ret = sched_getattr(pid, &sched_attr, sizeof(struct sched_attr), 0);
	if (ret) {
		perror("Failed to get attr");
		printf("Couldn't get schedattr for pid %d\n", pid);
		return -1;
	}

	if (sched_attr.sched_util_min != test_rt_min) {
		printf("pid %d has %d but default should be %d\n",
			pid, sched_attr.sched_util_min, test_rt_min);

		return -1;
	}

	pr_debug("pid %d has %d, default is %d\n",
		pid, sched_attr.sched_util_min, test_rt_min);

	return 0;
}

static int verify(void)
{
	int i, ret;

	/* flush any messages we printed into stdout */
	fflush(stdout);

	for (i = 0; i < NR_FORKS; i++) {

		/*
		 * If a pid is 0, it means we got an error.
		 *
		 * We carry on anyway to cleanup and not end up with zombie
		 * tasks.
		 */
		if (!pids[i])
			break;

		ret = verify_pid(pids[i]);
		if (ret)
			return ret;

		//pr_debug("%d policy = %d\n",
		//	 pids[i], sched_getscheduler(pids[i]));
	}

	return 0;
}

static void *test_loop(void *data)
{
	int ret;

	orig_rt_min = read_rt_min();

	while (nr_forks > 0) {
		if (test_rt_min > 1024)
			test_rt_min = 0;

		write_rt_min(++test_rt_min);
		ret = verify();
		if (ret)
			goto out;
	}

	printf("All forked RT tasks had the correct uclamp.min\n");

out:
	write_rt_min(orig_rt_min);
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t fork_thread, test_thread;
	int ret, i;

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


	for (i = 0; i < NR_FORKS; i++) {

		if (!pids[i])
			continue;

		/*
		 * Because of fork() parent and child don't see the same
		 * conditional variable, so we can't just signal them to
		 * wakeup.
		 *
		 * Since this is just a test, go brute force and just send
		 * SIGKILL.
		 */
		kill(pids[i], SIGKILL);
	}

	return EXIT_SUCCESS;
}
