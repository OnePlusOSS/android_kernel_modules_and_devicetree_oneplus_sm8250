// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/seq_file.h>
#include <../drivers/android/binder_internal.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <trace/hooks/binder.h>
#include <linux/random.h>

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <linux/sched_assist/sched_assist_common.h>
#endif
#include "sched_assist_binder.h"
#define CREATE_TRACE_POINTS
#include "binder_sched_trace.h"

unsigned int g_sched_enable = 1;
unsigned int g_sched_debug = 0;

unsigned int g_async_ux_enable = 1;
unsigned int g_set_last_async_ux = 0;
static unsigned int async_insert_queue = 1;
static unsigned int sync_insert_queue = 1;
static unsigned int async_ux_test = 0;
static unsigned int set_async_ux_after_pending = 0;

static int insert_limit[NUM_INSERT_MAX] = {0};

#define trace_binder_debug(x...) \
	do { \
		if (g_sched_debug) \
			trace_printk(x); \
	} while (0)

#define oplus_binder_debug(debug_mask, x...) \
	do { \
		if (g_sched_debug & debug_mask) \
			pr_info(x); \
	} while (0)

static inline int is_obs_valid(int async_ux_enable)
{
	if (async_ux_enable == ASYNC_UX_INIT)
		return OBS_INVALID;
	else
		return OBS_VALID;
}

static inline bool binder_is_sync_mode(u32 flags)
{
	return !(flags & TF_ONE_WAY);
}

void set_task_async_ux_enable(pid_t pid, int enable)
{
	struct task_struct *task = NULL;

	if (unlikely(!g_async_ux_enable)) {
		return;
	}
	if (enable >= ASYNC_UX_ENABLE_MAX) {
		trace_binder_set_get_ux(task, pid, enable, "set, enable error");
		return;
	}

	if (pid == CURRENT_TASK_PID) {
		task = current;
	} else {
		if (pid < 0 || pid > PID_MAX_DEFAULT) {
			trace_binder_set_get_ux(task, pid, enable, "set, pid error");
			return;
		}
		task = find_task_by_vpid(pid);
		if (IS_ERR_OR_NULL(task)) {
			trace_binder_set_get_ux(NULL, pid, enable, "set, task null");
			return;
		}
	}
	task->binder_async_ux_enable = enable;

	trace_binder_set_get_ux(task, pid, enable, "set enable end");
	oplus_binder_debug(BINDER_LOG_CRITICAL, "(set_pid=%d task_pid=%d comm=%s) enable=%d ux_sts=%d set enable end\n",
		pid, task->pid, task->comm, task->binder_async_ux_enable, task->binder_async_ux_sts);
}

bool get_task_async_ux_enable(pid_t pid)
{
	struct task_struct *task = NULL;
	int enable = 0;

	if (unlikely(!g_async_ux_enable)) {
		return false;
	}

	if (pid == CURRENT_TASK_PID) {
		task = current;
	} else {
		if (pid < 0 || pid > PID_MAX_DEFAULT) {
			trace_binder_set_get_ux(task, pid, enable, "get, pid error");
			return false;
		}
		task = find_task_by_vpid(pid);
		if (IS_ERR_OR_NULL(task)) {
			trace_binder_set_get_ux(NULL, pid, enable, "get, task null");
			return false;
		}
	}

	enable = task->binder_async_ux_enable;
	trace_binder_set_get_ux(task, pid, enable, "get end");
	return enable;
}

void get_all_tasks_async_ux_enable(void)
{
	struct task_struct *p = NULL;
	struct task_struct *t = NULL;
	bool async_ux_task = false;

	for_each_process_thread(p, t) {
		if (t->binder_async_ux_enable) {
			async_ux_task = true;
			pr_info("[async_ux_tasks] pid=%d tgid=%d comm=%s async_ux_enable=%d\n",
				t->pid, t->tgid, t->comm, t->binder_async_ux_enable);
			trace_binder_set_get_ux(t, INVALID_VALUE, t->binder_async_ux_enable, "[async_ux_tasks]");
		}
	}
	if (!async_ux_task) {
		pr_info("[async_ux_tasks] no async_ux task\n");
		trace_binder_set_get_ux(NULL, INVALID_VALUE, INVALID_VALUE,
			"[async_ux_tasks] no async_ux task");
	}
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)

static inline void sync_binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
	int from_depth = from_task->ux_depth;
	int from_state = from_task->ux_state;
	int type = get_ux_state_type(thread_task);

	if (type != UX_STATE_NONE && type != UX_STATE_INHERIT) {
		trace_binder_inherit_ux(from_task, thread_task, from_depth, from_state,
			type, INVALID_VALUE, sync, "sync_set_inherit_ux type not expected");
		return;
	}
	if (from_task && test_set_inherit_ux(from_task)) {
		if (!test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_depth, from_state);
			trace_binder_inherit_ux(from_task, thread_task, from_depth, from_state,
				type, INVALID_VALUE, sync, "set_inherit_ux set");
		} else {
			reset_inherit_ux(thread_task, from_task, INHERIT_UX_BINDER);
			trace_binder_inherit_ux(from_task, thread_task, from_depth,
				from_state, type, INVALID_VALUE, sync, "set_inherit_ux reset");
		}
	} else if (from_task && test_task_identify_ux(from_task, SA_TYPE_ID_CAMERA_PROVIDER)) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	} else if (from_task && test_task_is_rt(from_task)) { /* rt trans can be set as ux if binder thread is cfs class */
		if (!test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
			int ux_value = SA_TYPE_LIGHT;
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_depth, ux_value);
			trace_binder_inherit_ux(from_task, thread_task, from_depth,
				from_state, type, INVALID_VALUE, sync, "set_inherit_ux set TYPE_LIGHT");
		} else {
			trace_binder_inherit_ux(from_task, thread_task, from_depth,
				from_state, type, INVALID_VALUE, sync, "set_inherit_ux rt none");
		}
	}
#ifdef CONFIG_OPLUS_FEATURE_AUDIO_OPT
	else if (from_task && (is_audio_task(from_task))) {
		if (!test_task_ux(thread_task))
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_task->ux_depth, SA_TYPE_LIGHT);
	}
#endif
	else {
		trace_binder_inherit_ux(from_task, thread_task, from_depth, from_state,
			type, INVALID_VALUE, sync, "set_inherit_ux end do nothing");
	}
}

static inline void async_binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
	int type = 0;
	int ux_value = 0;

	if (unlikely(!g_async_ux_enable)) {
		return;
	}

	if (!thread_task) {
		return;
	}

	type = get_ux_state_type(thread_task);
	if (type != UX_STATE_NONE && type != UX_STATE_INHERIT) {
		trace_binder_inherit_ux(from_task, thread_task, INVALID_VALUE, INVALID_VALUE,
			type, INVALID_VALUE, sync, "async_set_inherit_ux type not expected");
		return;
	}

	trace_binder_inherit_ux(from_task, thread_task, thread_task->ux_depth, thread_task->ux_state,
		type, thread_task->binder_async_ux_sts, sync, "async_set_inherit_ux before set");

	ux_value = (thread_task->ux_state | SA_TYPE_HEAVY);
	set_inherit_ux(thread_task, INHERIT_UX_BINDER, thread_task->ux_depth, ux_value);
	thread_task->binder_async_ux_sts = true;

	trace_binder_inherit_ux(from_task, thread_task, thread_task->ux_depth, thread_task->ux_state,
		type, thread_task->binder_async_ux_sts, sync, "async_set_inherit_ux after set");

	oplus_binder_debug(BINDER_LOG_CRITICAL, "async_set_ux after set, current(pid=%d tgid=%d comm=%s) thread(pid=%d tgid=%d comm=%s) enable=%d ux_sts=%d\n",
		current->pid, current->tgid, current->comm, thread_task->pid, thread_task->tgid, thread_task->comm,
		thread_task->binder_async_ux_enable, thread_task->binder_async_ux_sts);
}

inline void binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
	if (sync) {
		sync_binder_set_inherit_ux(thread_task, from_task, sync);
	} else {
		async_binder_set_inherit_ux(thread_task, from_task, sync);
	}
}

inline void binder_unset_inherit_ux(struct task_struct *thread_task, bool sync)
{
	if (test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
		trace_binder_inherit_ux(NULL, thread_task, thread_task->ux_depth, thread_task->ux_state,
			INVALID_VALUE, thread_task->binder_async_ux_sts,
			sync, "unset_ux before unset");
		unset_inherit_ux(thread_task, INHERIT_UX_BINDER);
		if (!sync) {
			thread_task->binder_async_ux_sts = false;
		}
		trace_binder_inherit_ux(NULL, thread_task, thread_task->ux_depth, thread_task->ux_state,
			INVALID_VALUE, thread_task->binder_async_ux_sts, sync, "unset_ux after unset");
		if (!sync) {
			oplus_binder_debug(BINDER_LOG_CRITICAL, "async_unset_ux after unset, thread(pid=%d tgid=%d comm=%s) enable=%d ux_sts=%d\n",
				thread_task->pid, thread_task->tgid, thread_task->comm, thread_task->binder_async_ux_enable,
				thread_task->binder_async_ux_sts);
		}
	}
}

#else /* CONFIG_OPLUS_FEATURE_SCHED_ASSIST */
inline void binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
}

inline void binder_unset_inherit_ux(struct task_struct *thread_task, bool sync)
{
}
#endif

static int async_ux_test_debug(void)
{
	static unsigned int count = 0;
	unsigned int remainder = 0;
	int ret = 0;

	if (async_ux_test == ASYNC_UX_TEST_DISABLE) {
		return 0;
	}

	switch(async_ux_test) {
	case ASYNC_UX_RANDOM_LOW_INSERT_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % (2 * ASYNC_UX_ENABLE_MAX));
		break;
	case ASYNC_UX_RANDOM_HIGH_INSERT_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % ASYNC_UX_ENABLE_MAX);
		break;
	case ASYNC_UX_RANDOM_LOW_ENQUEUE_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % (2 * ASYNC_UX_ENABLE_MAX));
		if (ret > ASYNC_UX_ENABLE_ENQUEUE) {
			ret = 0;
		}
		break;
	case ASYNC_UX_RANDOM_HIGH_ENQUEUE_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % (ASYNC_UX_ENABLE_ENQUEUE + 1));
		break;
	case ASYNC_UX_INORDER_TEST:
		count++;
		remainder = count % 10;
		if (remainder == 1 || remainder == 5) {
			ret = ASYNC_UX_ENABLE_ENQUEUE;
		} else if (remainder == 2 || remainder == 6 || remainder == 8) {
			ret = ASYNC_UX_ENABLE_INSERT_QUEUE;
		} else {
			ret = ASYNC_UX_DISABLE;
		}
		break;
	default:
		ret = 0;
		break;
	}
	if (ret >= ASYNC_UX_ENABLE_MAX) {
		ret = 0;
	}
	return ret;
}

void android_vh_alloc_oem_binder_struct_handler(struct binder_transaction_data *tr,
    struct binder_transaction *t, struct binder_proc *target_proc)
{
	int binder_async_ux_enable = 0, test_debug = 0, origin_async_ux_enable = 0;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}
	if (IS_ERR_OR_NULL(tr) || IS_ERR_OR_NULL(t)) {
		trace_binder_ux_enable(current, binder_async_ux_enable, t, "tr_buf t/tr err");
		return;
	}

	if (binder_is_sync_mode(tr->flags)) {
		return;
	}

	origin_async_ux_enable = t->async_ux_enable;
	if (origin_async_ux_enable == ASYNC_UX_DISABLE) {
		trace_binder_ux_enable(current, origin_async_ux_enable, t, "tr_buf NOT_ASYNC_UX");
		return;
	}

	if (is_obs_valid(origin_async_ux_enable) == OBS_VALID) {
		trace_binder_ux_enable(current, origin_async_ux_enable, t, "tr_buf async_ux has enable");
		return;
	}

	binder_async_ux_enable = current->binder_async_ux_enable;
	test_debug = async_ux_test_debug();
	if ((binder_async_ux_enable != ASYNC_UX_INIT && binder_async_ux_enable) || test_debug) {
		t->async_ux_enable = binder_async_ux_enable ? binder_async_ux_enable : test_debug;
		trace_binder_ux_enable(current, t->async_ux_enable, t, "async_ux enable");
	} else {
		trace_binder_ux_enable(current, binder_async_ux_enable, t,
				"tr_buf async_ux not enable");
		t->async_ux_enable = ASYNC_UX_DISABLE;
	}
}

/* sync mode unset_ux: pls refer to android_vh_sync_txn_recvd_handler / android_vh_binder_priority_skip_handler / android_vh_binder_wait_for_work_handler  */
static void sync_mode_unset_ux(struct binder_transaction *t,
				struct binder_proc *proc, struct binder_thread *thread, bool finished)
{
}

static void async_mode_unset_ux(struct binder_transaction *t,
				struct binder_proc *proc, struct binder_thread *thread, bool finished)
{
	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}

	if (IS_ERR_OR_NULL(thread) || IS_ERR_OR_NULL(thread->task)) {
		return;
	}

	if (!thread->task->binder_async_ux_sts) {
		trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, thread->task,
			INVALID_VALUE, t, "async_ux unset sts false");
		return;
	} else {
		trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, thread->task,
			INVALID_VALUE, t, "async_ux unset sts true");
	}

	if (finished) {	/* t has been freed */
		binder_unset_inherit_ux(thread->task, false);
		trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, thread->task,
			INVALID_VALUE, t, "async_ux unset ux[finished]");
		return;
	}
	if (IS_ERR_OR_NULL(t)) {
		return;
	}
	if (is_obs_valid(t->async_ux_enable) != OBS_VALID) {
		trace_binder_ux_task(0, INVALID_VALUE, INVALID_VALUE, NULL, INVALID_VALUE,
			t, "async_ux flag invalid return");
		return;
	}
	if (t->async_ux_enable == ASYNC_UX_DISABLE) {
		return;
	}
	t->async_ux_enable = ASYNC_UX_DISABLE;
	binder_unset_inherit_ux(thread->task, false);
	trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, thread->task,
		t->async_ux_enable, t, "async_ux unset ux[not-finished]");
}

static void set_binder_thread_mode(struct binder_transaction *t,
	struct task_struct *task, bool sync, bool reset)
{
	struct binder_node *node = NULL;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable) || (!g_set_last_async_ux)) {
		return;
	}

	if (IS_ERR_OR_NULL(task)) {
		return;
	}

	if (t && !IS_ERR_OR_NULL(t->buffer)) {
		node = t->buffer->target_node;
	}
	oplus_binder_debug(BINDER_LOG_DEBUG, "before set, thread(pid=%d tgid=%d comm=%s) sync: %d, reset: %d, node: 0x%llx, mode: %d\n",
		task->pid, task->tgid, task->comm, sync, reset, (unsigned long long)task->binder_thread_node, task->binder_thread_mode);
	if (!reset && sync) {
		task->binder_thread_mode = THREAD_MODE_SYNC;
		task->binder_thread_node = node;
		trace_set_thread_mode(task, node, sync, "sync mode");
	} else if (!reset && !sync) {
		task->binder_thread_mode = THREAD_MODE_ASYNC;
		task->binder_thread_node = node;
		trace_set_thread_mode(task, node, sync, "async mode");
	} else {	/* reset */
		task->binder_thread_mode = THREAD_MODE_UNKNOWN;
		task->binder_thread_node = NULL;
		trace_set_thread_mode(task, NULL, sync, "reset");
	}
}

void android_vh_binder_transaction_received_handler(struct binder_transaction *t,
    struct binder_proc *proc, struct binder_thread *thread, uint32_t cmd)
{
	if(proc && proc->tsk && (!strncmp(proc->tsk->comm, SYSTEM_SERVER_NAME, TASK_COMM_LEN))) {
		if(t->debug_id == insert_limit[NUM_INSERT_ID1]) {
			insert_limit[NUM_INSERT_ID1] = 0;
		} else if (t->debug_id == insert_limit[NUM_INSERT_ID2]) {
			insert_limit[NUM_INSERT_ID2] = 0;
		}
	}

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable) || (!set_async_ux_after_pending)) {
		return;
	}
	if (IS_ERR_OR_NULL(t)) {
		return;
	}
	if (binder_is_sync_mode(t->flags)) {
		return;
	}
	if (is_obs_valid(t->async_ux_enable) != OBS_VALID) {
		return;
	}
	if (t->async_ux_enable <= ASYNC_UX_DISABLE || t->async_ux_enable >= ASYNC_UX_ENABLE_MAX) {
		return;
	}
	if (IS_ERR_OR_NULL(thread) || IS_ERR_OR_NULL(thread->task)) {
		return;
	}
	trace_binder_ux_task(0, INVALID_VALUE, INVALID_VALUE, thread->task, INVALID_VALUE,
		t, "async_ux set when received");
	binder_set_inherit_ux(thread->task, NULL, false);
}

void android_vh_binder_free_buf_handler(struct binder_proc *proc,
    struct binder_thread *thread, struct binder_buffer *buffer)
{
	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}

	if (IS_ERR_OR_NULL(thread) || IS_ERR_OR_NULL(thread->task)) {
		return;
	}
	if (buffer->async_transaction) {
		async_mode_unset_ux(buffer->transaction, proc, thread, true);
		set_binder_thread_mode(NULL, thread->task, false, true);
		trace_binder_free_buf(proc, thread, buffer, "async mode");
	} else {
		sync_mode_unset_ux(buffer->transaction, proc, thread, true);
		set_binder_thread_mode(NULL, thread->task, true, true);
		trace_binder_free_buf(proc, thread, buffer, "sync mode");
	}
}

static bool binder_dynamic_enqueue_work_ilocked(struct binder_work *work,
		struct list_head *target_list, bool sync_insert)
{
	struct binder_work *w = NULL;
	struct binder_transaction *t = NULL;
	bool insert = false;
	int i = 0;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return false;
	}

	trace_binder_ux_work(work, target_list, NULL, insert, i, "dynamic begin");
	BUG_ON(target_list == NULL);
	BUG_ON(work->entry.next && !list_empty(&work->entry));

	if(insert_limit[NUM_INSERT_ID1] && insert_limit[NUM_INSERT_ID2])
		goto end;

	list_for_each_entry(w, target_list, entry) {
		i++;
		if (i > MAX_UX_IN_LIST) {
			insert = false;
			break;
		}
		if (IS_ERR_OR_NULL(w)) {
			break;
		}

		if (w->type != BINDER_WORK_TRANSACTION) {
			continue;
		}

		t = container_of(w, struct binder_transaction, work);
		if (IS_ERR_OR_NULL(t)) {
			break;
		}
		if (sync_insert) {
			if (!binder_is_sync_mode(t->flags)) {
				continue;
			}
			if (!t->from) {
				continue;
			}
			if ((test_task_ux(t->from->task) || test_task_is_rt(t->from->task))) {
				continue;
			}
		} else {
			if (binder_is_sync_mode(t->flags)) {
				continue;
			}
			if (is_obs_valid(t->async_ux_enable) != OBS_VALID) {
				insert = true;
				break;
			}
			if (t->async_ux_enable) {
				continue;
			}
		}
		insert = true;
		break;
	}
end:
	if (insert && !IS_ERR_OR_NULL(w) && !IS_ERR_OR_NULL(&w->entry)) {
		list_add(&work->entry, &w->entry);
		if(!insert_limit[NUM_INSERT_ID1] && (t->debug_id != insert_limit[NUM_INSERT_ID2])) {
			insert_limit[NUM_INSERT_ID1] = t->debug_id;
		} else if (!insert_limit[NUM_INSERT_ID2] && (t->debug_id != insert_limit[NUM_INSERT_ID1])) {
			insert_limit[NUM_INSERT_ID2] = t->debug_id;
		}
	} else {
		list_add_tail(&work->entry, target_list);
	}
	trace_binder_ux_work(work, target_list, IS_ERR_OR_NULL(w) ? NULL : &w->entry, insert, i, "dynamic end");
	return true;
}

void android_vh_binder_special_task_handler(struct binder_transaction *t,
	struct binder_proc *proc, struct binder_thread *thread, struct binder_work *w,
	struct list_head *target_list, bool sync, bool *enqueue_task)
{
	bool allow_sync_insert = false;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)
		|| unlikely(!async_insert_queue)) {
		return;
	}

	if (sync) {
		/* called by binder_proc_transaction() when no binder_thread selected */
		if (sync_insert_queue && t && proc && (&proc->todo == target_list)) {
			if(proc->tsk && t->from
				&& (test_set_inherit_ux(t->from->task) || test_task_is_rt(t->from->task))
				&& !strncmp(proc->tsk->comm, SYSTEM_SERVER_NAME, TASK_COMM_LEN)) {
				allow_sync_insert = true;
				goto dynamic_enqueue;
			}
		}
		return;
	}

	if (unlikely(!async_insert_queue)) {
		return;
	}
	if (!w || !target_list) {
		return;
	}

	if (!t && w) {
		t = container_of(w, struct binder_transaction, work);
		if (!t) {
			return;
		}
	}
	if (is_obs_valid(t->async_ux_enable) != OBS_VALID) {
		return;
	}
dynamic_enqueue:
	if ((!IS_ERR_OR_NULL(t) && t->async_ux_enable == ASYNC_UX_ENABLE_INSERT_QUEUE) || allow_sync_insert) {
		if (binder_dynamic_enqueue_work_ilocked(w, target_list, allow_sync_insert)) {
					/*
			if enqueue_task == false, binder_dynamic_enqueue_work_ilocked list_add_xxx is called,
			don't call binder.c binder_enqueue_work_ilocked() again.
			*/
			*enqueue_task = false;
		}
	}
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)

static bool sync_mode_set_ux(struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool sync)
{
	struct task_struct *binder_proc_task = proc->tsk;
	bool set_ux = true;

	if (unlikely(!g_sched_enable)) {
		return false;
	}

	if (!strncmp(binder_proc_task->comm, "servicemanager", TASK_COMM_LEN)
				|| !strncmp(binder_proc_task->comm, "hwservicemanage", TASK_COMM_LEN)) {
		binder_set_inherit_ux(binder_proc_task, current, sync);
		trace_binder_proc_thread(binder_proc_task, binder_th_task, sync, INVALID_VALUE, t, proc,
			"sync_ux set SF ux");
	}

	if (!binder_th_task)
		return false;

	trace_binder_proc_thread(binder_proc_task, binder_th_task, sync, INVALID_VALUE, t, proc,
		"sync_ux set ux");

	return set_ux;
}

#define CHECK_MAX_NODE_FOR_ASYNC_THREAD		400
static struct task_struct *get_current_async_thread(struct binder_transaction *t, struct binder_proc *proc)
{
	struct rb_node *n = NULL;
	struct binder_node *node = NULL;
	struct binder_thread *thread = NULL;
	ktime_t time = 0;
	int count = 0;
	bool has_async = true;

	if (unlikely(!g_set_last_async_ux)) {
		return NULL;
	}
	if (proc->max_threads <= 0) {
		return NULL;
	}
	if (t && t->buffer) {
		node = t->buffer->target_node;
	}
	if (!node) {
		return NULL;
	}
	time = ktime_get();
	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		thread = rb_entry(n, struct binder_thread, rb_node);
		if (thread->task) {
			if ((thread->task->binder_thread_mode == THREAD_MODE_ASYNC)
				&& (thread->task->binder_thread_node == node)) {
				time = ktime_get() - time;
				trace_get_async_thread(proc, thread, count, NULL, node, has_async, time, "async_thread got");
				oplus_binder_debug(BINDER_LOG_INFO, "proc(pid:%d tgid:%d comm:%s) thread(pid:%d tgid:%d comm:%s) \
					max_threads:%d request:%d started:%d count:%d node:0x%llx time:0x%lldns got\n",
					proc ? proc->tsk->pid : 0,
					proc ? proc->tsk->tgid : 0,
					proc ? proc->tsk->comm : "null",
					thread ? thread->task->pid : 0,
					thread ? thread->task->tgid : 0,
					thread ? thread->task->comm : "null",
					proc ? proc->max_threads : 0,
					proc ? proc->requested_threads : 0,
					proc ? proc->requested_threads_started : 0,
					count, (unsigned long long)node, time);
				return thread->task;
			}
		}
		if (node->has_async_transaction == false) {
			has_async = false;
			break;
		}
		count++;
		if (count > CHECK_MAX_NODE_FOR_ASYNC_THREAD) {
			break;
		}
		if (g_sched_debug) {
			trace_get_async_thread(proc, thread, count, thread->task->binder_thread_node, node,
				has_async, time, "async_thread search");
			oplus_binder_debug(BINDER_LOG_DEBUG, "proc(pid:%d tgid:%d comm:%s) thread(pid:%d tgid:%d comm:%s) \
				max_threads:%d request:%d started:%d count:%d ots_node:0x%llx node:0x%llx time:%lldns\n",
				proc ? proc->tsk->pid : 0,
				proc ? proc->tsk->tgid : 0,
				proc ? proc->tsk->comm : "null",
				thread ? thread->task->pid : 0,
				thread ? thread->task->tgid : 0,
				thread ? thread->task->comm : "null",
				proc ? proc->max_threads : 0,
				proc ? proc->requested_threads : 0,
				proc ? proc->requested_threads_started : 0,
				count, (unsigned long long)(thread->task->binder_thread_node), (unsigned long long)node, time);
		}
	}
	time = ktime_get() - time;
	trace_get_async_thread(proc, thread, count, NULL, node, has_async, time, "async_thread get null");
	oplus_binder_debug(BINDER_LOG_INFO, "proc(pid:%d tgid:%d comm:%s) max_threads:%d request:%d \
		started:%d count:%d node:0x%llx has_async:%d time:%lldns get null\n",
		proc ? proc->tsk->pid : 0,
		proc ? proc->tsk->tgid : 0,
		proc ? proc->tsk->comm : "null",
		proc ? proc->max_threads : 0,
		proc ? proc->requested_threads : 0,
		proc ? proc->requested_threads_started : 0,
		count, (unsigned long long)node, has_async, time);
	return NULL;
}

/* need to double check whether set the same task ux twice is ok or not */
static bool async_mode_set_ux(struct binder_proc *proc, struct binder_transaction *t,
		struct task_struct *binder_th_task, bool sync, bool pending_async,
		struct task_struct **last_task, bool *force_sync)
{
	struct task_struct *ux_task = binder_th_task;
	bool set_ux = false;

	if (unlikely(!g_sched_enable)) {
		return false;
	}

	if (unlikely(!g_async_ux_enable)) {
		return false;
	}

	if (is_obs_valid(t->async_ux_enable) != OBS_VALID) {
		set_ux = false;
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, INVALID_VALUE,
			t, "async_ux enable flag invalid return");
		goto end;
	}

	if (t->async_ux_enable == ASYNC_UX_DISABLE) {
		set_ux = false;
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, t->async_ux_enable,
			t, "async_ux not enable");
		goto end;
	}

	if (ux_task) {
		set_ux = true;
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, t->async_ux_enable,
			t, "async_ux set ux");
		goto end;
	}

	/* pending_async, no binder_th_task */
	if (pending_async) {
		ux_task = get_current_async_thread(t, proc);
		if (ux_task) {
			*last_task = ux_task;
			set_ux = true;
		} else {
			set_ux = false;
		}
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, t->async_ux_enable,
			t, "async_ux set last as ux");
		goto end;
	}
end:
	trace_binder_ux_task(sync, pending_async, set_ux, ux_task, INVALID_VALUE,
			t, "async_ux end");
	return set_ux;
}


#else /* CONFIG_OPLUS_FEATURE_SCHED_ASSIST */

static bool sync_mode_set_ux(struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool sync)
{
	return false;
}

static bool async_mode_set_ux(struct binder_proc *proc, struct binder_transaction *t,
	struct task_struct *binder_th_task, bool sync, bool pending_async,
	struct task_struct **last_task, bool *force_sync)
{
	return false;
}

#endif

void android_vh_binder_proc_transaction_finish_handler(struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool pending_async, bool sync)
{
	struct task_struct *last_task = NULL;
	bool set_ux = false;
	bool force_sync = false;

	if (unlikely(!g_sched_enable))
		return;

	if (sync) {
		return;
	}

	set_binder_thread_mode(t, binder_th_task, sync, false);

	if (sync) {
		set_ux = sync_mode_set_ux(proc, t, binder_th_task, sync);
	} else {
		set_ux = async_mode_set_ux(proc, t, binder_th_task, sync,
			pending_async, &last_task, &force_sync);
	}
	if (set_ux) {
		if (force_sync) {
			binder_set_inherit_ux(binder_th_task, current, true);
		} else if (last_task) {
			binder_set_inherit_ux(last_task, current, sync);
		} else {
			binder_set_inherit_ux(binder_th_task, current, sync);
		}
	}

	if (last_task) {
		trace_binder_ux_task(sync, pending_async, set_ux, last_task,
			INVALID_VALUE, t, "ux t_finish last");
	} else {
		trace_binder_ux_task(sync, pending_async, set_ux, binder_th_task,
			INVALID_VALUE, t, "ux t_finish");
	}
}

module_param_named(binder_sched_enable, g_sched_enable, uint, 0660);
module_param_named(binder_sched_debug, g_sched_debug, uint, 0660);
module_param_named(binder_async_ux_test, async_ux_test, uint, 0660);
module_param_named(binder_ux_enable, g_async_ux_enable, int, 0664);
module_param_named(binder_async_insert_queue, async_insert_queue, int, 0664);
module_param_named(binder_sync_insert_queue, sync_insert_queue, uint, 0664);
module_param_named(binder_set_last_async_ux, g_set_last_async_ux, int, 0664);
module_param_named(binder_set_async_ux_after_pending, set_async_ux_after_pending, int, 0664);
