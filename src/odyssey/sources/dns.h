#ifndef ODYSSEY_DNS_H
#define ODYSSEY_DNS_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

int od_getaddrname(struct addrinfo *, char *, int, int, int);
int od_getpeername(machine_io_t *, char *, int, int, int);
int od_getsockname(machine_io_t *, char *, int, int, int);

#endif /* ODYSSEY_DNS_H */
