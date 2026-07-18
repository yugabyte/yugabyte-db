#ifndef ODYSSEY_RESET_H
#define ODYSSEY_RESET_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

int od_reset(od_server_t *);
int yb_send_reset_backend_default_query(od_server_t *server);

#endif /* ODYSSEY_RESET_H */
