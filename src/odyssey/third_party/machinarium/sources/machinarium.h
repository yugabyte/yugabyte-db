#ifndef MACHINARIUM_H
#define MACHINARIUM_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#ifdef __cplusplus
extern "C" {
#endif
#include "c.h"
#include "zpq_stream.h"
#include "bind.h"

#include "macro.h"
#include "channel_limit.h"

#if __GNUC__ >= 4
#define MACHINE_API __attribute__((visibility("default")))
#else
#define MACHINE_API
#endif

typedef void (*machine_coroutine_t)(void *arg);

#define mm_yield machine_sleep(0);

/* library handles */

typedef struct machine_cond_private machine_cond_t;
typedef struct machine_msg_private machine_msg_t;
typedef struct machine_channel_private machine_channel_t;
typedef struct machine_tls_private machine_tls_t;
typedef struct machine_iov_private machine_iov_t;
typedef struct machine_io_private machine_io_t;

/* configuration */

MACHINE_API void machinarium_set_stack_size(int size);

MACHINE_API void machinarium_set_pool_size(int size);

MACHINE_API void machinarium_set_coroutine_cache_size(int size);

MACHINE_API void machinarium_set_msg_cache_gc_size(int size);

/* main */

MACHINE_API int machinarium_init(void);

MACHINE_API void machinarium_free(void);

/* machine control */

MACHINE_API int64_t machine_create(char *name, machine_coroutine_t, void *arg);

MACHINE_API void machine_stop_current(void);

MACHINE_API int machine_active(void);

MACHINE_API uint64_t machine_self(void);

MACHINE_API void **machine_thread_private(void);

MACHINE_API int machine_wait(uint64_t machine_id);

MACHINE_API int machine_stop(uint64_t machine_id);

/* time */

MACHINE_API uint64_t machine_time_ms(void);

MACHINE_API uint64_t machine_time_us(void);

MACHINE_API uint32_t machine_timeofday_sec(void);

MACHINE_API void
machine_stat(uint64_t *coroutine_count, uint64_t *coroutine_cache_count,
	     uint64_t *msg_allocated, uint64_t *msg_cache_count,
	     uint64_t *msg_cache_gc_count, uint64_t *msg_cache_size);

/* signals */

MACHINE_API int machine_signal_init(sigset_t *, sigset_t *);

MACHINE_API int machine_signal_wait(uint32_t time_ms);

/* coroutine */

MACHINE_API int64_t machine_coroutine_create(machine_coroutine_t, void *arg);

MACHINE_API void machine_sleep(uint32_t time_ms);

MACHINE_API int machine_join(uint64_t coroutine_id);

MACHINE_API int machine_cancel(uint64_t coroutine_id);

MACHINE_API int machine_cancelled(void);

MACHINE_API int machine_timedout(void);

MACHINE_API int machine_errno(void);

/* condition */

MACHINE_API machine_cond_t *machine_cond_create(void);

MACHINE_API void machine_cond_free(machine_cond_t *);

MACHINE_API void machine_cond_propagate(machine_cond_t *, machine_cond_t *);

MACHINE_API void machine_cond_signal(machine_cond_t *);

MACHINE_API int machine_cond_try(machine_cond_t *);

MACHINE_API int machine_cond_wait(machine_cond_t *, uint32_t time_ms);

/* msg */

MACHINE_API machine_msg_t *machine_msg_create(int reserve);

MACHINE_API machine_msg_t *machine_msg_create_or_advance(machine_msg_t *,
							 int size);

MACHINE_API void machine_msg_free(machine_msg_t *);

MACHINE_API void machine_msg_set_type(machine_msg_t *, int type);

MACHINE_API int machine_msg_type(machine_msg_t *);

MACHINE_API void *machine_msg_data(machine_msg_t *);

MACHINE_API int machine_msg_size(machine_msg_t *);

MACHINE_API int machine_msg_write(machine_msg_t *, void *buf, int size);

/* channel */

MACHINE_API machine_channel_t *machine_channel_create();

MACHINE_API void machine_channel_free(machine_channel_t *);

MACHINE_API void
machine_channel_assign_limit_policy(machine_channel_t *obj, int limit,
				    mm_channel_limit_policy_t policy);

MACHINE_API mm_retcode_t machine_channel_write(machine_channel_t *,
					       machine_msg_t *);

MACHINE_API machine_msg_t *machine_channel_read(machine_channel_t *,
						uint32_t time_ms);

/* tls */

MACHINE_API machine_tls_t *machine_tls_create(void);

MACHINE_API void machine_tls_free(machine_tls_t *);

MACHINE_API int machine_tls_set_verify(machine_tls_t *, char *);

MACHINE_API int machine_tls_set_server(machine_tls_t *, char *);

MACHINE_API int machine_tls_set_protocols(machine_tls_t *, char *);

MACHINE_API int machine_tls_set_ca_path(machine_tls_t *, char *);

MACHINE_API int machine_tls_set_ca_file(machine_tls_t *, char *);

MACHINE_API int machine_tls_set_cert_file(machine_tls_t *, char *);

MACHINE_API int machine_tls_set_key_file(machine_tls_t *, char *);

/* io control */

MACHINE_API machine_io_t *machine_io_create(void);

MACHINE_API void machine_io_free(machine_io_t *);

MACHINE_API int machine_io_attach(machine_io_t *);

MACHINE_API int machine_io_detach(machine_io_t *);

MACHINE_API char *machine_error(machine_io_t *);

MACHINE_API int machine_fd(machine_io_t *);

MACHINE_API int machine_set_nodelay(machine_io_t *, int enable);

MACHINE_API int machine_set_keepalive(machine_io_t *, int enable, int delay,
				      int interval, int probes,
				      int usr_timeout);

MACHINE_API int machine_set_tls(machine_io_t *, machine_tls_t *, uint32_t);
MACHINE_API int machine_set_compression(machine_io_t *, char algorithm);

MACHINE_API int machine_io_verify(machine_io_t *, char *common_name);

/* dns */

MACHINE_API int machine_getsockname(machine_io_t *, struct sockaddr *, int *);

MACHINE_API int machine_getpeername(machine_io_t *, struct sockaddr *, int *);

MACHINE_API int machine_getaddrinfo(char *addr, char *service,
				    struct addrinfo *hints,
				    struct addrinfo **res, uint32_t time_ms);

/* io */

MACHINE_API int machine_connect(machine_io_t *, struct sockaddr *,
				uint32_t time_ms);

MACHINE_API int machine_connected(machine_io_t *);

MACHINE_API int machine_bind(machine_io_t *, struct sockaddr *, int);

MACHINE_API int machine_accept(machine_io_t *, machine_io_t **, int backlog,
			       int attach, uint32_t time_ms);

MACHINE_API int machine_eventfd(machine_io_t *);

MACHINE_API int machine_close(machine_io_t *);

MACHINE_API int machine_shutdown(machine_io_t *);

MACHINE_API int machine_shutdown_receptions(machine_io_t *);

/* iov */

MACHINE_API machine_iov_t *machine_iov_create(void);

MACHINE_API void machine_iov_free(machine_iov_t *);

MACHINE_API int machine_iov_add_pointer(machine_iov_t *, void *, int);

MACHINE_API int machine_iov_add(machine_iov_t *, machine_msg_t *);

MACHINE_API int machine_iov_pending(machine_iov_t *);

/* read */

MACHINE_API int machine_read_active(machine_io_t *);

MACHINE_API int machine_read_start(machine_io_t *, machine_cond_t *);

MACHINE_API int machine_read_stop(machine_io_t *);

MACHINE_API ssize_t machine_read_raw(machine_io_t *, void *, size_t);

MACHINE_API machine_msg_t *machine_read(machine_io_t *, size_t,
					uint32_t time_ms);

/* write */

MACHINE_API int machine_write_start(machine_io_t *, machine_cond_t *);

MACHINE_API int machine_write_stop(machine_io_t *);

MACHINE_API ssize_t machine_write_raw(machine_io_t *, void *, size_t, size_t *);

MACHINE_API ssize_t machine_writev_raw(machine_io_t *, machine_iov_t *);

MACHINE_API int machine_write(machine_io_t *, machine_msg_t *,
			      uint32_t time_ms);

/* lrand48 */
MACHINE_API long int machine_lrand48(void);

/* compression */
MACHINE_API char
machine_compression_choose_alg(char *client_compression_algorithms);

#ifdef __cplusplus
}
#endif

#endif /* MACHINARIUM_H */
