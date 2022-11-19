/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Qais Yousef */
#ifndef __UCLAMP_TEST_THERMAL_PRESSURE_EVENTS_H__
#define __UCLAMP_TEST_THERMAL_PRESSURE_EVENTS_H__

struct rq_pelt_event {
	unsigned long long ts;
	int cpu;
	unsigned long util_avg;
	unsigned long capacity_orig;
	unsigned long thermal_avg;
	unsigned long uclamp_min;
	unsigned long uclamp_max;
	int overutilized;
};

#endif /* __UCLAMP_TEST_THERMAL_PRESSURE_EVENTS_H__ */
