
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <arpa/inet.h>
#include <netinet/in.h>

#include <machinarium.h>
#include <odyssey.h>

static int od_getsockaddrname(struct sockaddr *sa, char *buf, int size,
			      int add_addr, int add_port)
{
	char addr[128];
	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		inet_ntop(sa->sa_family, &sin->sin_addr, addr, sizeof(addr));
		if (add_addr && add_port)
			od_snprintf(buf, size, "%s:%d", addr,
				    ntohs(sin->sin_port));
		else if (add_addr)
			od_snprintf(buf, size, "%s", addr);
		else if (add_port)
			od_snprintf(buf, size, "%d", ntohs(sin->sin_port));
		return 0;
	}
	if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)sa;
		inet_ntop(sa->sa_family, &sin->sin6_addr, addr, sizeof(addr));
		if (add_addr && add_port)
			od_snprintf(buf, size, "[%s]:%d", addr,
				    ntohs(sin->sin6_port));
		else if (add_addr)
			od_snprintf(buf, size, "%s", addr);
		else if (add_port)
			od_snprintf(buf, size, "%d", ntohs(sin->sin6_port));
		return 0;
	}
	if (sa->sa_family == AF_UNIX) {
		od_snprintf(buf, size, "<unix socket>");
		return 0;
	}
	od_snprintf(buf, size, "%s", "");
	return -1;
}

int od_getaddrname(struct addrinfo *ai, char *buf, int size, int add_addr,
		   int add_port)
{
	return od_getsockaddrname(ai->ai_addr, buf, size, add_addr, add_port);
}

int od_getpeername(machine_io_t *io, char *buf, int size, int add_addr,
		   int add_port)
{
	struct sockaddr_storage sa;
	int salen = sizeof(sa);
	int rc = machine_getpeername(io, (struct sockaddr *)&sa, &salen);
	if (rc < 0) {
		od_snprintf(buf, size, "%s", "");
		return -1;
	}
	return od_getsockaddrname((struct sockaddr *)&sa, buf, size, add_addr,
				  add_port);
}

int od_getsockname(machine_io_t *io, char *buf, int size, int add_addr,
		   int add_port)
{
	struct sockaddr_storage sa;
	int salen = sizeof(sa);
	int rc = machine_getsockname(io, (struct sockaddr *)&sa, &salen);
	if (rc < 0) {
		od_snprintf(buf, size, "%s", "");
		return -1;
	}
	return od_getsockaddrname((struct sockaddr *)&sa, buf, size, add_addr,
				  add_port);
}
