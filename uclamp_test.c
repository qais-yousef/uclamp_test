/* SPDX-License-Identifier: GPL-2.0 */

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NR_FORKS	100

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
	}

	return NULL;
child:
	pthread_mutex_lock(&mutex);
	pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);

	return NULL;
}

static void *test_loop(void *data)
{
	while (nr_forks > 0) {
		sleep(1);
		write_rt_min(300);
	}
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

		//pr_debug("%d policy = %d\n",
		//	 pids[i], sched_getscheduler(pids[i]));

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

	write_rt_min(orig_rt_min);

	printf("All forked RT tasks had the correct uclamp.min\n");
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	pthread_t fork_thread, test_thread;
	int ret;

	orig_rt_min = read_rt_min();

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
