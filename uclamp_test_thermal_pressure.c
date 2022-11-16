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

#define NR_LOOPS	1000000

bool done = false;

//#define DEBUG
#ifdef DEBUG
#define pr_debug	printf
#else
#define pr_debug(...)
#endif


static void *thread_loop(void *data)
{
	volatile int loops = NR_LOOPS;

	while (!done) {

		while (loops--);

		loops = NR_LOOPS;

		usleep(500);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t thread;
	int ret;

	ret = pthread_create(&thread, NULL, thread_loop, NULL);
	if (ret) {
		perror("Failed to create thread");
		return EXIT_FAILURE;
	}

	done = true;
	pthread_join(thread, NULL);

	return EXIT_SUCCESS;
}
