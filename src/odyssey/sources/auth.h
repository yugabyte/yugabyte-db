#ifndef ODYSSEY_AUTH_H
#define ODYSSEY_AUTH_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

int od_auth_frontend(od_client_t *);
int od_auth_backend(od_server_t *, machine_msg_t *, od_client_t *);

#endif /* ODYSSEY_AUTH_H */
