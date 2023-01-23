/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Qais Yousef */
#ifndef __UCLAMP_TEST_THERMAL_PRESSURE_EVENTS_H__
#define __UCLAMP_TEST_THERMAL_PRESSURE_EVENTS_H__

struct rq_pelt_event {
	unsigned long long ts;
	int cpu;
	unsigned long rq_util_avg;
	unsigned long p_util_avg;
	unsigned long capacity_orig;
	unsigned long thermal_avg;
	unsigned long uclamp_min;
	unsigned long uclamp_max;
	int overutilized;
	int misfit;
};

struct select_task_rq_fair_event {
	unsigned long long ts;
	int cpu;
	unsigned long p_util_avg;
	unsigned long uclamp_min;
	unsigned long uclamp_max;
};

struct compute_energy_event {
	unsigned long long ts;
	int dst_cpu;
	unsigned long p_util_avg;
	unsigned long uclamp_min;
	unsigned long uclamp_max;
	unsigned long energy;
};

#endif /* __UCLAMP_TEST_THERMAL_PRESSURE_EVENTS_H__ */
