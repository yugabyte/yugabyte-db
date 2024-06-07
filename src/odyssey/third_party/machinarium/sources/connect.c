
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

static void mm_connect_on_write_cb(mm_fd_t *handle)
{
	mm_io_t *io = handle->on_write_arg;
	mm_call_t *call = &io->call;
	if (mm_call_is_aborted(call))
		return;
	call->status = mm_socket_error(handle->fd);
	mm_scheduler_wakeup(&mm_self->scheduler, call->coroutine);
}

static int mm_connect(mm_io_t *io, struct sockaddr *sa, uint32_t time_ms)
{
	mm_machine_t *machine = mm_self;
	mm_errno_set(0);

	if (mm_call_is_active(&io->call)) {
		mm_errno_set(EINPROGRESS);
		return -1;
	}
	if (io->connected) {
		mm_errno_set(EINPROGRESS);
		return -1;
	}

	/* create socket */
	int rc;
	rc = mm_io_socket(io, sa);
	if (rc == -1)
		goto error;

	/* start connection */
	rc = mm_socket_connect(io->fd, sa);
	if (rc == 0) {
		rc = machine_io_attach((machine_io_t *)io);
		if (rc == -1)
			goto error;
		goto done;
	}

	assert(rc == -1);
	if (errno != EINPROGRESS) {
		mm_errno_set(errno);
		goto error;
	}

	/* add socket to event loop */
	rc = machine_io_attach((machine_io_t *)io);
	if (rc == -1)
		goto error;

	/* subscribe for connection event */
	rc = mm_loop_write(&machine->loop, &io->handle, mm_connect_on_write_cb,
			   io);
	if (rc == -1) {
		mm_errno_set(errno);
		goto error;
	}

	/* wait for completion */
	mm_call(&io->call, MM_CALL_CONNECT, time_ms);

	rc = mm_loop_write_stop(&machine->loop, &io->handle);
	if (rc == -1) {
		mm_errno_set(errno);
		goto error;
	}

	rc = io->call.status;
	if (rc != 0) {
		mm_loop_delete(&machine->loop, &io->handle);
		mm_errno_set(rc);
		goto error;
	}

done:
	assert(!io->call.timedout);
	io->connected = 1;
	return 0;

error:
	if (io->fd != -1) {
		close(io->fd);
		io->fd = -1;
	}
	io->handle.fd = -1;
	io->attached = 0;
	return -1;
}

MACHINE_API int machine_connect(machine_io_t *obj, struct sockaddr *sa,
				uint32_t time_ms)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	int rc = mm_connect(io, sa, time_ms);
	if (rc == -1)
		return -1;
	return 0;
}

MACHINE_API int machine_connected(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	return io->connected;
}
