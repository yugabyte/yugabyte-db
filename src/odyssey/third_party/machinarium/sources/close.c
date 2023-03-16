
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

MACHINE_API int machine_close(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	if (io->fd == -1) {
		mm_errno_set(EBADF);
		return -1;
	}
	if (io->attached)
		machine_io_detach(obj);
	int rc;
	rc = close(io->fd);
	if (rc == -1)
		mm_errno_set(errno);
	io->connected = 0;
	io->fd = -1;
	io->handle.fd = -1;
	io->handle.on_read = NULL;
	io->handle.on_write = NULL;
	return 0;
}
