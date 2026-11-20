#ifndef MM_FD_H
#define MM_FD_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

/* machinarium file descriptor */

typedef struct mm_fd mm_fd_t;

/*
 * YB: MM_RDHUP watches a socket for the peer's hangup (EPOLLRDHUP, plus the
 * EPOLLHUP and EPOLLERR epoll always reports) without watching it for data.
 * It calls on_read, like MM_R, and the two are never set together.
 */
enum { MM_R = 1, MM_W = 2, MM_RDHUP = 4 };

typedef void (*mm_fd_callback_t)(mm_fd_t *);

struct mm_fd {
	int fd;
	int mask;
	mm_fd_callback_t on_read;
	void *on_read_arg;
	mm_fd_callback_t on_write;
	void *on_write_arg;
};

#endif /* MM_FD_H */
