#ifndef ODYSSEY_BACKEND_H
#define ODYSSEY_BACKEND_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

int od_backend_connect(od_server_t *, char *, kiwi_params_t *, od_client_t *);
int od_backend_connect_cancel(od_server_t *, od_rule_storage_t *, kiwi_key_t *);

void od_backend_close_connection(od_server_t *);
void od_backend_close(od_server_t *);
void od_backend_error(od_server_t *, char *, char *, uint32_t);

int od_backend_update_parameter(od_server_t *, char *, char *, uint32_t, int);
int od_backend_ready(od_server_t *, char *, uint32_t);
int od_backend_ready_wait(od_server_t *, char *, int, uint32_t);

od_retcode_t od_backend_query_send(od_server_t *server, char *context,
				   char *query, char *param, int len);
od_retcode_t od_backend_query(od_server_t *, char *, char *, char *, int,
			      uint32_t, uint32_t);

#endif /* ODYSSEY_BACKEND_H */
