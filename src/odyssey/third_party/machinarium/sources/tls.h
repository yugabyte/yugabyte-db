#ifndef MM_TLS_H
#define MM_TLS_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

void mm_tls_engine_init(void);
void mm_tls_engine_free(void);

static inline int mm_tls_is_active(mm_io_t *io)
{
	return io->tls_ssl != NULL;
}

void mm_tls_init(mm_io_t *);
void mm_tls_free(mm_io_t *);
void mm_tls_error_reset(mm_io_t *);
int mm_tls_handshake(mm_io_t *, uint32_t);
int mm_tls_write(mm_io_t *, char *, int);
int mm_tls_writev(mm_io_t *, struct iovec *, int);
int mm_tls_read_pending(mm_io_t *);
int mm_tls_read(mm_io_t *, char *, int);
int mm_tls_verify_common_name(mm_io_t *, char *);
SSL_CTX *mm_tls_get_context(mm_io_t *io, int is_client);

#endif /* MM_TLS_H */
