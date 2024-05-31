#ifndef MM_SOCKET_H
#define MM_SOCKET_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

int mm_socket(int, int, int);
int mm_socket_eventfd(unsigned int);
int mm_socket_set_nonblock(int, int);
int mm_socket_set_nodelay(int, int);
int mm_socket_set_keepalive(int fd, int enable, int delay, int interval,
			    int keep_count, int usr_timeout);
int mm_socket_set_nosigpipe(int, int);
int mm_socket_set_reuseaddr(int, int);
int mm_socket_set_reuseport(int, int);
int mm_socket_set_ipv6only(int, int);
int mm_socket_error(int);
int mm_socket_connect(int, struct sockaddr *);
int mm_socket_bind(int, struct sockaddr *);
int mm_socket_listen(int, int);
int mm_socket_accept(int, struct sockaddr *, socklen_t *);
int mm_socket_write(int, void *, int);
int mm_socket_writev(int, struct iovec *, int);
int mm_socket_read(int, void *, int);
int mm_socket_getsockname(int, struct sockaddr *, socklen_t *);
int mm_socket_getpeername(int, struct sockaddr *, socklen_t *);
int mm_socket_getaddrinfo(char *, char *, struct addrinfo *,
			  struct addrinfo **);

#endif /* MM_SOCKET_H */
