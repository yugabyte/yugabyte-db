
#include <machinarium.h>
#include <machinarium_private.h>

MACHINE_API int machine_shutdown(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);

	int rc = shutdown(io->fd, SHUT_RDWR);

	if (rc == -1)
		return MM_NOTOK_RETCODE;

	return MM_OK_RETCODE;
}

MACHINE_API int machine_shutdown_receptions(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);

	int rc = shutdown(io->fd, SHUT_RD);

	if (rc == -1)
		return MM_NOTOK_RETCODE;

	return MM_OK_RETCODE;
}
