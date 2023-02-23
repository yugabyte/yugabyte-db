
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

MACHINE_API machine_tls_t *machine_tls_create(void)
{
	mm_errno_set(0);
	mm_tls_t *tls;
	tls = malloc(sizeof(*tls));
	if (tls == NULL) {
		mm_errno_set(ENOMEM);
		return NULL;
	}
	tls->verify = MM_TLS_NONE;
	tls->server = NULL;
	tls->protocols = NULL;
	tls->ca_path = NULL;
	tls->ca_file = NULL;
	tls->cert_file = NULL;
	tls->key_file = NULL;
	return (machine_tls_t *)tls;
}

MACHINE_API void machine_tls_free(machine_tls_t *obj)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	mm_errno_set(0);
	if (tls->protocols)
		free(tls->protocols);
	if (tls->server)
		free(tls->server);
	if (tls->ca_path)
		free(tls->ca_path);
	if (tls->ca_file)
		free(tls->ca_file);
	if (tls->cert_file)
		free(tls->cert_file);
	if (tls->key_file)
		free(tls->key_file);
	free(tls);
}

MACHINE_API int machine_tls_set_verify(machine_tls_t *obj, char *mode)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	if (strcasecmp(mode, "none") == 0)
		tls->verify = MM_TLS_NONE;
	else if (strcasecmp(mode, "peer") == 0)
		tls->verify = MM_TLS_PEER;
	else if (strcasecmp(mode, "peer_strict") == 0)
		tls->verify = MM_TLS_PEER_STRICT;
	else
		return -1;
	return 0;
}

MACHINE_API int machine_tls_set_server(machine_tls_t *obj, char *name)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	mm_errno_set(0);
	char *string = strdup(name);
	if (string == NULL) {
		mm_errno_set(ENOMEM);
		return -1;
	}
	if (tls->server)
		free(tls->server);
	tls->server = string;
	return 0;
}

MACHINE_API int machine_tls_set_protocols(machine_tls_t *obj, char *protocols)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	mm_errno_set(0);
	char *string = strdup(protocols);
	if (string == NULL) {
		mm_errno_set(ENOMEM);
		return -1;
	}
	if (tls->protocols)
		free(tls->protocols);
	tls->protocols = string;
	return 0;
}

MACHINE_API int machine_tls_set_ca_path(machine_tls_t *obj, char *path)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	mm_errno_set(0);
	char *string = strdup(path);
	if (string == NULL) {
		mm_errno_set(ENOMEM);
		return -1;
	}
	if (tls->ca_path)
		free(tls->ca_path);
	tls->ca_path = string;
	return 0;
}

MACHINE_API int machine_tls_set_ca_file(machine_tls_t *obj, char *path)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	mm_errno_set(0);
	char *string = strdup(path);
	if (string == NULL) {
		mm_errno_set(ENOMEM);
		return -1;
	}
	if (tls->ca_file)
		free(tls->ca_file);
	tls->ca_file = string;
	return 0;
}

MACHINE_API int machine_tls_set_cert_file(machine_tls_t *obj, char *path)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	mm_errno_set(0);
	char *string = strdup(path);
	if (string == NULL) {
		mm_errno_set(ENOMEM);
		return -1;
	}
	if (tls->cert_file)
		free(tls->cert_file);
	tls->cert_file = string;
	return 0;
}

MACHINE_API int machine_tls_set_key_file(machine_tls_t *obj, char *path)
{
	mm_tls_t *tls = mm_cast(mm_tls_t *, obj);
	mm_errno_set(0);
	char *string = strdup(path);
	if (string == NULL) {
		mm_errno_set(ENOMEM);
		return -1;
	}
	if (tls->key_file)
		free(tls->key_file);
	tls->key_file = string;
	return 0;
}

MACHINE_API int machine_set_tls(machine_io_t *obj, machine_tls_t *tls,
				uint32_t timeout)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	if (io->tls) {
		mm_errno_set(EINPROGRESS);
		return -1;
	}
	io->tls = mm_cast(mm_tls_t *, tls);
	return mm_tls_handshake(io, timeout);
}

MACHINE_API int machine_set_compression(machine_io_t *obj, char algorithm)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	if (io->zpq_stream) {
		mm_errno_set(EINPROGRESS);
		return -1;
	}

	int impl = mm_zpq_get_algorithm_impl(algorithm);
	if (impl >= 0) {
		io->zpq_stream =
			zpq_create(impl, (mm_zpq_tx_func)mm_io_write,
				   (mm_zpq_rx_func)mm_io_read, obj, NULL, 0);
		return 0;
	}
	return -1;
}

MACHINE_API machine_io_t *machine_io_create(void)
{
	mm_errno_set(0);
	mm_io_t *io = malloc(sizeof(*io));
	if (io == NULL) {
		mm_errno_set(ENOMEM);
		return NULL;
	}
	memset(io, 0, sizeof(*io));
	io->fd = -1;
	mm_tls_init(io);
	return (machine_io_t *)io;
}

MACHINE_API void machine_io_free(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	mm_tls_free(io);
	mm_compression_free(io);
	free(io);
}

MACHINE_API char *machine_error(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	if (io->tls_error)
		return io->tls_error_msg;
	int errno_ = mm_errno_get();
	if (errno_)
		return strerror(errno_);
	errno_ = errno;
	if (errno_)
		return strerror(errno_);
	return NULL;
}

MACHINE_API int machine_fd(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	return io->fd;
}

MACHINE_API int machine_set_nodelay(machine_io_t *obj, int enable)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	io->opt_nodelay = enable;
	if (io->fd != -1) {
		int rc;
		rc = mm_socket_set_nodelay(io->fd, enable);
		if (rc == -1) {
			mm_errno_set(errno);
			return -1;
		}
	}
	return 0;
}

MACHINE_API int machine_set_keepalive(machine_io_t *obj, int enable, int delay,
				      int interval, int probes, int usr_timeout)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	io->opt_keepalive = enable;
	io->opt_keepalive_delay = delay;
	io->opt_keepalive_interval = interval;
	io->opt_keepalive_probes = probes;
	io->opt_keepalive_usr_timeout = usr_timeout;
	if (io->fd != -1) {
		int rc;
		rc = mm_socket_set_keepalive(io->fd, enable, delay, interval,
					     probes, usr_timeout);
		if (rc == -1) {
			mm_errno_set(errno);
			return -1;
		}
	}
	return 0;
}

MACHINE_API int machine_io_attach(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	if (io->attached) {
		mm_errno_set(EINPROGRESS);
		return -1;
	}
	int rc;
	rc = mm_loop_add(&mm_self->loop, &io->handle, 0);
	if (rc == -1) {
		mm_errno_set(errno);
		return -1;
	}
	io->attached = 1;
	return 0;
}

MACHINE_API int machine_io_detach(machine_io_t *obj)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	if (!io->attached) {
		mm_errno_set(ENOTCONN);
		return -1;
	}
	int rc;
	rc = mm_loop_delete(&mm_self->loop, &io->handle);
	if (rc == -1) {
		mm_errno_set(errno);
		return -1;
	}
	io->attached = 0;
	return 0;
}

MACHINE_API int machine_io_verify(machine_io_t *obj, char *common_name)
{
	mm_io_t *io = mm_cast(mm_io_t *, obj);
	mm_errno_set(0);
	if (io->tls == NULL) {
		mm_errno_set(EINVAL);
		return -1;
	}
	int rc;
	rc = mm_tls_verify_common_name(io, common_name);
	return rc;
}

int mm_io_socket_set(mm_io_t *io, int fd)
{
	io->fd = fd;
	int rc;
	rc = mm_socket_set_nosigpipe(io->fd, 1);
	if (rc == -1) {
		mm_errno_set(errno);
		return -1;
	}
	rc = mm_socket_set_nonblock(io->fd, 1);
	if (rc == -1) {
		mm_errno_set(errno);
		return -1;
	}
	if (!io->is_unix_socket) {
		if (io->opt_nodelay) {
			rc = mm_socket_set_nodelay(io->fd, 1);
			if (rc == -1) {
				mm_errno_set(errno);
				return -1;
			}
		}
		if (io->opt_keepalive) {
			rc = mm_socket_set_keepalive(
				io->fd, 1, io->opt_keepalive_delay,
				io->opt_keepalive_interval,
				io->opt_keepalive_probes,
				io->opt_keepalive_usr_timeout);
			if (rc == -1) {
				mm_errno_set(errno);
				return -1;
			}
		}
	}
	io->handle.fd = io->fd;
	return 0;
}

int mm_io_socket(mm_io_t *io, struct sockaddr *sa)
{
	if (sa->sa_family == AF_UNIX)
		io->is_unix_socket = 1;
	int fd;
	fd = mm_socket(sa->sa_family, SOCK_STREAM, 0);
	if (fd == -1) {
		mm_errno_set(errno);
		return -1;
	}
	return mm_io_socket_set(io, fd);
}

ssize_t mm_io_write(mm_io_t *io, void *buf, size_t size)
{
	mm_errno_set(0);
	ssize_t rc;
	if (mm_tls_is_active(io))
		rc = mm_tls_write(io, buf, size);
	else
		rc = mm_socket_write(io->fd, buf, size);
	if (rc > 0)
		return rc;
	int errno_ = errno;
	mm_errno_set(errno_);
	if (errno_ == EAGAIN || errno_ == EWOULDBLOCK || errno_ == EINTR)
		return -1;
	io->connected = 0;
	return -1;
}

ssize_t mm_io_read(mm_io_t *io, void *buf, size_t size)
{
	mm_errno_set(0);
	ssize_t rc;
	if (mm_tls_is_active(io))
		rc = mm_tls_read(io, buf, size);
	else
		rc = mm_socket_read(io->fd, buf, size);

	if (rc > 0) {
		return rc;
	}
	if (rc < 0) {
		int errno_ = errno;
		mm_errno_set(errno_);
		if (errno_ == EAGAIN || errno_ == EWOULDBLOCK ||
		    errno_ == EINTR)
			return -1;
	}
	/* error of eof */
	io->connected = 0;
	return rc;
}
