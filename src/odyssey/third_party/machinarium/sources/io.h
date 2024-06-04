#ifndef MM_IO_H
#define MM_IO_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_tls mm_tls_t;
typedef struct mm_tls_ctx mm_tls_ctx_t;
typedef struct mm_io mm_io_t;

typedef enum { MM_TLS_NONE, MM_TLS_PEER, MM_TLS_PEER_STRICT } mm_tlsverify_t;

struct mm_tls {
	mm_tlsverify_t verify;
	char *server;
	char *protocols;
	char *ca_path;
	char *ca_file;
	char *cert_file;
	char *key_file;
};

struct mm_tls_ctx {
	mm_tls_t *key;
	SSL_CTX *tls_ctx;
	mm_tls_ctx_t *next;
};

struct mm_io {
	int fd;
	mm_fd_t handle;
	int attached;
	int is_unix_socket;
	int is_eventfd;
	int opt_nodelay;
	/* tcp keepalive */
	int opt_keepalive;
	int opt_keepalive_delay;
	int opt_keepalive_interval;
	int opt_keepalive_probes;
	int opt_keepalive_usr_timeout;
	/* tls */
	mm_tls_t *tls;
	SSL *tls_ssl;
	int tls_error;
	char tls_error_msg[128];
	/* connect */
	int connected;
	/* accept */
	int accepted;
	int accept_listen;
	/* io */
	machine_cond_t *on_read;
	machine_cond_t *on_write;
	mm_call_t call;
	/* compression */
	mm_zpq_stream_t *zpq_stream;
};

int mm_io_socket_set(mm_io_t *, int);
int mm_io_socket(mm_io_t *, struct sockaddr *);
ssize_t mm_io_write(mm_io_t *, void *, size_t);
ssize_t mm_io_read(mm_io_t *, void *, size_t);

#endif /* MM_IO_H */
