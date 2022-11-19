/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Qais Yousef */
#ifndef __EVENTS_DEFS_H__
#define __EVENTS_DEFS_H__

#define INIT_EVENT_RB(event)	struct ring_buffer *event##_rb = NULL

#define CREATE_EVENT_RB(event) do {							\
		event##_rb = ring_buffer__new(bpf_map__fd(skel->maps.event##_rb),	\
					      handle_##event##_event, NULL, NULL);	\
		if (!event##_rb) {							\
			ret = -1;							\
			fprintf(stderr, "Failed to create " #event " ringbuffer\n");	\
			goto cleanup;							\
		}									\
	} while(0)

#define DESTROY_EVENT_RB(event) do {							\
		ring_buffer__free(event##_rb);						\
	} while(0)

#define POLL_EVENT_RB(event) do {							\
		ret = ring_buffer__poll(event##_rb, 1000);				\
		if (ret == -EINTR) {							\
			ret = 0;							\
			break;								\
		}									\
		if (ret < 0) {								\
			fprintf(stderr, "Error polling " #event " ring buffer: %d\n", ret); \
			break;								\
		}									\
		pr_debug(stdout, "[" #event "] consumed %d events\n", ret);		\
	} while(0)

#define INIT_EVENT_THREAD(event) pthread_t event##_tid

#define CREATE_EVENT_THREAD(event) do {							\
		ret = pthread_create(&event##_tid, NULL, event##_thread_fn, NULL);	\
		if (ret) {								\
			fprintf(stderr, "Failed to create " #event " thread: %d\n", ret); \
			goto cleanup;							\
		}									\
	} while(0)

#define DESTROY_EVENT_THREAD(event) do {						\
		ret = pthread_join(event##_tid, NULL);					\
		if (ret)								\
			fprintf(stderr, "Failed to destory " #event " thread: %d\n", ret); \
	} while(0)

#define EVENT_THREAD_FN(event)								\
	void *event##_thread_fn(void *data)						\
	{										\
		int ret;								\
		INIT_EVENT_RB(event);							\
		CREATE_EVENT_RB(event);							\
		while (!done) {							\
			POLL_EVENT_RB(event);						\
			usleep(10000);							\
		}									\
	cleanup:									\
		DESTROY_EVENT_RB(event);						\
		return NULL;								\
	}
#endif /* __EVENTS_DEFS_H__ */
