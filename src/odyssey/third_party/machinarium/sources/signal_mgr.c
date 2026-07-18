
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

static void mm_signalmgr_on_read(mm_fd_t *handle)
{
	mm_signalmgr_t *mgr = handle->on_read_arg;

	struct signalfd_siginfo fdsi;
	int rc;
	rc = mm_socket_read(mgr->fd.fd, &fdsi, sizeof(fdsi));
	(void)rc;
	assert(rc == sizeof(fdsi));

	if (mgr->readers_count == 0)
		return;

	/* do one-time wakeup and detach all readers */
	mm_list_t *i, *n;
	mm_list_foreach_safe(&mgr->readers, i, n)
	{
		mm_signalrd_t *reader;
		reader = mm_container_of(i, mm_signalrd_t, link);
		reader->signal = fdsi.ssi_signo;
		mm_scheduler_wakeup(&mm_self->scheduler,
				    reader->call.coroutine);
		mm_list_unlink(&reader->link);
	}
}

int mm_signalmgr_init(mm_signalmgr_t *mgr, mm_loop_t *loop)
{
	mgr->readers_count = 0;
	mm_list_init(&mgr->readers);
	memset(&mgr->fd, 0, sizeof(mgr->fd));
	mgr->fd.fd = -1;

	sigset_t mask;
	sigemptyset(&mask);
	int rc;
	rc = signalfd(-1, &mask, SFD_NONBLOCK);
	if (rc == -1)
		return -1;
	mgr->fd.fd = rc;

	rc = mm_loop_add(loop, &mgr->fd, 0);
	if (rc == -1) {
		close(mgr->fd.fd);
		mgr->fd.fd = -1;
		return -1;
	}
	rc = mm_loop_read(loop, &mgr->fd, mm_signalmgr_on_read, mgr);
	if (rc == -1) {
		mm_loop_delete(loop, &mgr->fd);
		close(mgr->fd.fd);
		mgr->fd.fd = -1;
		return -1;
	}
	return 0;
}

void mm_signalmgr_free(mm_signalmgr_t *mgr, mm_loop_t *loop)
{
	if (mgr->fd.fd == -1)
		return;
	mm_loop_delete(loop, &mgr->fd);
	close(mgr->fd.fd);
	mgr->fd.fd = -1;
}

int mm_signalmgr_set(mm_signalmgr_t *mgr, sigset_t *set, sigset_t *ignore)
{
	int rc;
	rc = signalfd(mgr->fd.fd, set, SFD_NONBLOCK);
	if (rc == -1)
		return -1;
	assert(rc == mgr->fd.fd);
	sigset_t mask;
	sigfillset(&mask);
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
	pthread_sigmask(SIG_BLOCK, set, NULL);
	pthread_sigmask(SIG_BLOCK, ignore, NULL);
	return 0;
}

int mm_signalmgr_wait(mm_signalmgr_t *mgr, uint32_t time_ms)
{
	mm_errno_set(0);

	mm_signalrd_t reader;
	reader.signal = 0;
	mm_list_init(&reader.link);
	mm_list_append(&mgr->readers, &reader.link);
	mgr->readers_count++;

	mm_call(&reader.call, MM_CALL_SIGNAL, time_ms);

	if (reader.call.status != 0) {
		/* timedout or cancel */
		if (!reader.signal) {
			assert(mgr->readers_count > 0);
			mgr->readers_count--;
			mm_list_unlink(&reader.link);
		}
		return -1;
	}

	assert(reader.signal > 0);
	return reader.signal;
}

MACHINE_API int machine_signal_init(sigset_t *set, sigset_t *ignore)
{
	return mm_signalmgr_set(&mm_self->signal_mgr, set, ignore);
}

MACHINE_API int machine_signal_wait(uint32_t time_ms)
{
	return mm_signalmgr_wait(&mm_self->signal_mgr, time_ms);
}
