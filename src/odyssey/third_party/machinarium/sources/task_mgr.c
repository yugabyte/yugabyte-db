
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

enum { MM_TASK, MM_TASK_EXIT };

static void mm_taskmgr_main(void *arg __attribute__((unused)))
{
	sigset_t mask;
	sigfillset(&mask);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
	for (;;) {
		mm_msg_t *msg;
		msg = mm_channel_read(&machinarium.task_mgr.channel,
				      UINT32_MAX);
		assert(msg != NULL);
		if (msg->type == MM_TASK_EXIT) {
			free(msg);
			break;
		}
		assert(msg->type == MM_TASK);
		assert(mm_buf_used(&msg->data) == sizeof(mm_task_t));

		mm_task_t *task;
		task = (mm_task_t *)msg->data.start;
		task->function(task->arg);
		int event_mgr_fd;
		event_mgr_fd = mm_eventmgr_signal(&task->on_complete);
		if (event_mgr_fd > 0)
			mm_eventmgr_wakeup(event_mgr_fd);
	}
}

void mm_taskmgr_init(mm_taskmgr_t *mgr)
{
	mgr->workers_count = 0;
	mgr->workers = NULL;
	mm_channel_init(&mgr->channel);
}

int mm_taskmgr_start(mm_taskmgr_t *mgr, int workers_count)
{
	mgr->workers_count = workers_count;
	mgr->workers = malloc(sizeof(int) * workers_count);
	if (mgr->workers == NULL)
		return -1;
	int i = 0;
	for (; i < workers_count; i++) {
		char name[32];
		mm_snprintf(name, sizeof(name), "resolver: %d", i);
		mgr->workers[i] = machine_create(name, mm_taskmgr_main, NULL);
	}
	return 0;
}

void mm_taskmgr_stop(mm_taskmgr_t *mgr)
{
	int i;
	int rc;
	for (i = 0; i < mgr->workers_count; i++) {
		mm_msg_t *msg;
		msg = malloc(sizeof(mm_msg_t));
		if (msg == NULL) {
			/* todo: */
			abort();
			return;
		}
		mm_msg_init(msg, MM_TASK_EXIT);
		mm_channel_write(&mgr->channel, msg);
	}
	for (i = 0; i < mgr->workers_count; i++) {
		rc = machine_wait(mgr->workers[i]);
		if (rc != MM_OK_RETCODE) {
			/* TODO: handle gracefully */
			abort();
			return;
		}
	}
	mm_channel_free(&mgr->channel);
	free(mgr->workers);
}

int mm_taskmgr_new(mm_taskmgr_t *mgr, mm_task_function_t function, void *arg,
		   uint32_t time_ms)
{
	mm_msg_t *msg;
	msg = (mm_msg_t *)machine_msg_create(sizeof(mm_task_t));
	if (msg == NULL)
		return -1;
	msg->type = MM_TASK;

	mm_task_t *task;
	task = (mm_task_t *)msg->data.start;
	task->function = function;
	task->arg = arg;
	mm_eventmgr_add(&mm_self->event_mgr, &task->on_complete);

	/* schedule task */
	mm_channel_write(&mgr->channel, msg);

	/* wait for completion */
	time_ms = UINT32_MAX;

	int ready;
	ready = mm_eventmgr_wait(&mm_self->event_mgr, &task->on_complete,
				 time_ms);
	if (!ready) {
		/* todo: */
		abort();
		return 0;
	}

	machine_msg_free((machine_msg_t *)msg);
	return 0;
}
