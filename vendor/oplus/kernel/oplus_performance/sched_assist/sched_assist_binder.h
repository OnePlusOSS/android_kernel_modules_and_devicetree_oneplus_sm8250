/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_SCHED_BINDER_H_
#define _OPLUS_SCHED_BINDER_H_
#include "sched_assist_common.h"
#if defined(CONFIG_OPLUS_FEATURE_ASYNC_BINDER_INHERIT_UX)
#include "sched_assist_locking.h"
#include <linux/sched.h>
#include <uapi/linux/android/binder.h>

#define SET_ASYNC_UX_ENABLE				0x45555801
#define ASYNC_UX_ENABLE_DATA_SIZE		4

#define CURRENT_TASK_PID				-1
#define SYSTEM_SERVER_NAME              "system_server"

enum OBS_STATUS {
	 OBS_INVALID,
	 OBS_VALID,
	 OBS_NOT_ASYNC_UX,
};

#define INVALID_VALUE           -1
#define MAX_UX_IN_LIST			20

enum ASYNC_UX_TEST_ITEM {
	 ASYNC_UX_TEST_DISABLE,
	 ASYNC_UX_RANDOM_LOW_INSERT_TEST,
	 ASYNC_UX_RANDOM_HIGH_INSERT_TEST,
	 ASYNC_UX_RANDOM_LOW_ENQUEUE_TEST,
	 ASYNC_UX_RANDOM_HIGH_ENQUEUE_TEST,
	 ASYNC_UX_INORDER_TEST,
};

enum ASYNC_UX_ENABLE_ITEM {
	ASYNC_UX_INIT = -1,
	ASYNC_UX_DISABLE,
	ASYNC_UX_ENABLE_ENQUEUE,
	ASYNC_UX_ENABLE_INSERT_QUEUE,
	ASYNC_UX_ENABLE_MAX,
};

enum BINDER_THREAD_MODE {
	THREAD_MODE_UNKNOWN,
	THREAD_MODE_SYNC,
	THREAD_MODE_ASYNC,
};

enum {
	BINDER_LOG_CRITICAL		= 1U << 0,
	BINDER_LOG_INFO			= 1U << 1,
	BINDER_LOG_DEBUG		= 1U << 2,
};

enum {
	NUM_INSERT_ID1,
	NUM_INSERT_ID2,
	NUM_INSERT_MAX,
};

struct binder_transaction;
struct binder_proc;
struct binder_thread;
struct binder_work;
struct binder_buffer;
struct binder_transaction_data;

extern void binder_set_inherit_ux(struct task_struct *thread_task, struct task_struct *from_task, bool sync);
extern void binder_unset_inherit_ux(struct task_struct *thread_task, bool sync);
extern void android_vh_binder_special_task_handler(struct binder_transaction *t,
		struct binder_proc *proc, struct binder_thread *thread, struct binder_work *w,
		struct list_head *target_list, bool sync, bool *enqueue_task);
extern void android_vh_binder_proc_transaction_finish_handler(struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool pending_async, bool sync);
extern void android_vh_binder_free_buf_handler(struct binder_proc *proc,
		struct binder_thread *thread, struct binder_buffer *buffer);
extern void android_vh_binder_transaction_received_handler(struct binder_transaction *t,
		struct binder_proc *proc, struct binder_thread *thread, uint32_t cmd);
extern void android_vh_alloc_oem_binder_struct_handler(struct binder_transaction_data *tr,
		struct binder_transaction *t, struct binder_proc *target_proc);
#else
static inline void binder_set_inherit_ux(struct task_struct *thread_task, struct task_struct *from_task)
{
	if (from_task && test_set_inherit_ux(from_task)) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, from_task->ux_state);
		else
			reset_inherit_ux(thread_task, from_task, INHERIT_UX_BINDER);
	} else if (from_task && test_task_identify_ux(from_task, SA_TYPE_ID_CAMERA_PROVIDER)) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	} else if (from_task && (from_task->sched_class == &rt_sched_class)) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	}
#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
	else if (from_task && (is_audio_task(from_task))) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	}
#endif
}
static inline void binder_unset_inherit_ux(struct task_struct *thread_task)
{
	if (test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
		unset_inherit_ux(thread_task, INHERIT_UX_BINDER);
	}
}
#endif /* defined(CONFIG_OPLUS_FEATURE_ASYNC_BINDER_INHERIT_UX) */
extern const struct sched_class rt_sched_class;
static inline void binder_set_inherit_ux_listpick(struct task_struct *thread_task, struct task_struct *from_task)
{
	if (!test_task_ux(thread_task)) {
		set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT+UX_PRIORITY_TOP_APP);
	}
}
#endif
