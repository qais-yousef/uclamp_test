/*
 * Based on:
 *
 *	https://github.com/scheduler-tools/rt-app/blob/master/libdl/dl_syscalls.h
 *	https://github.com/scheduler-tools/rt-app/blob/master/libdl/dl_syscalls.c
 *
 * Libdl
 *  (C) Dario Faggioli <raistlin@linux.it>, 2009, 2010
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License (COPYING file) for more details.
 *
 */

#ifndef __SCHED_H__
#define __SCHED_H__

#include <linux/kernel.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>
#include <linux/types.h>

/* __NR_sched_setattr number */
#ifndef __NR_sched_setattr
#ifdef __x86_64__
#define __NR_sched_setattr		314
#endif

#ifdef __i386__
#define __NR_sched_setattr		351
#endif

#ifdef __arm__
#define __NR_sched_setattr		380
#endif

#ifdef __aarch64__
#define __NR_sched_setattr		274
#endif
#endif

/* __NR_sched_getattr number */
#ifndef __NR_sched_getattr
#ifdef __x86_64__
#define __NR_sched_getattr		315
#endif

#ifdef __i386__
#define __NR_sched_getattr		352
#endif

#ifdef __arm__
#define __NR_sched_getattr		381
#endif

#ifdef __aarch64__
#define __NR_sched_getattr		275
#endif
#endif
/*
 * For the sched_{set,get}attr() calls
 */
#define SCHED_FLAG_RESET_ON_FORK        0x01
#define SCHED_FLAG_RECLAIM              0x02
#define SCHED_FLAG_DL_OVERRUN           0x04
#define SCHED_FLAG_KEEP_POLICY          0x08
#define SCHED_FLAG_KEEP_PARAMS          0x10
#define SCHED_FLAG_UTIL_CLAMP_MIN       0x20
#define SCHED_FLAG_UTIL_CLAMP_MAX       0x40

#define SCHED_FLAG_KEEP_ALL 	(SCHED_FLAG_KEEP_POLICY | \
				 SCHED_FLAG_KEEP_PARAMS)

#define SCHED_FLAG_UTIL_CLAMP	(SCHED_FLAG_UTIL_CLAMP_MIN | \
				 SCHED_FLAG_UTIL_CLAMP_MAX)

#define SCHED_FLAG_ALL	(SCHED_FLAG_RESET_ON_FORK	| \
			 SCHED_FLAG_RECLAIM		| \
			 SCHED_FLAG_DL_OVERRUN		| \
			 SCHED_FLAG_KEEP_ALL		| \
			 SCHED_FLAG_UTIL_CLAMP)

struct sched_attr {
	__u32 size;

	__u32 sched_policy;
	__u64 sched_flags;

	/* SCHED_NORMAL, SCHED_BATCH */
	__s32 sched_nice;

	/* SCHED_FIFO, SCHED_RR */
	__u32 sched_priority;

	/* SCHED_DEADLINE */
	__u64 sched_runtime;
	__u64 sched_deadline;
	__u64 sched_period;

	/* Utilization hints */
	__u32 sched_util_min;
	__u32 sched_util_max;
};

static inline int sched_setattr(pid_t pid, const struct sched_attr *attr,
				unsigned int flags)
{
	return syscall(__NR_sched_setattr, pid, attr, flags);
}

static inline int sched_getattr(pid_t pid, struct sched_attr *attr,
				unsigned int size, unsigned int flags)
{
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

#endif /* __SCHED_H__ */
