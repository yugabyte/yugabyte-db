
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

typedef struct {
	char *addr;
	char *service;
	struct addrinfo *hints;
	struct addrinfo **res;
	int rc;
} mm_getaddrinfo_t;

static void mm_getaddrinfo_cb(void *arg)
{
	mm_getaddrinfo_t *gai = arg;
	gai->rc = mm_socket_getaddrinfo(gai->addr, gai->service, gai->hints,
					gai->res);
}

MACHINE_API int machine_getaddrinfo(char *addr, char *service,
				    struct addrinfo *hints,
				    struct addrinfo **res, uint32_t time_ms)
{
	mm_getaddrinfo_t gai = { .addr = addr,
				 .service = service,
				 .hints = hints,
				 .res = res,
				 .rc = 0 };
	int rc;
	rc = mm_taskmgr_new(&machinarium.task_mgr, mm_getaddrinfo_cb, &gai,
			    time_ms);
	if (rc == -1)
		return -1;
	return gai.rc;
}

MACHINE_API int machine_getsockname(machine_io_t *obj, struct sockaddr *sa,
				    int *salen)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	socklen_t slen = *salen;
	int rc = mm_socket_getsockname(io->fd, sa, &slen);
	if (rc < 0) {
		mm_errno_set(errno);
		return -1;
	}
	*salen = slen;
	return 0;
}

MACHINE_API int machine_getpeername(machine_io_t *obj, struct sockaddr *sa,
				    int *salen)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	socklen_t slen = *salen;
	int rc = mm_socket_getpeername(io->fd, sa, &slen);
	if (rc < 0) {
		mm_errno_set(errno);
		return -1;
	}
	*salen = slen;
	return 0;
}
