#ifndef ODYSSEY_TLS_H
#define ODYSSEY_TLS_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

machine_tls_t *od_tls_frontend(od_config_listen_t *);

int od_tls_frontend_accept(od_client_t *, od_logger_t *, od_config_listen_t *,
			   machine_tls_t *);

machine_tls_t *od_tls_backend(od_tls_opts_t *);

int od_tls_backend_connect(od_server_t *, od_logger_t *, od_tls_opts_t *);

#endif /* ODYSSEY_TLS_H */
