#ifndef ODYSSEY_AUTH_QUERY_H
#define ODYSSEY_AUTH_QUERY_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define ODYSSEY_AUTH_QUERY_MAX_PASSSWORD_LEN 4096

int od_auth_query(od_client_t *, char *);

#endif /* ODYSSEY_AUTH_QUERY_H */
