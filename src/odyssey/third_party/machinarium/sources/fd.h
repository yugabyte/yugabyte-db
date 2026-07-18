#ifndef MM_FD_H
#define MM_FD_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

/* machinarium file descriptor */

typedef struct mm_fd mm_fd_t;

enum { MM_R = 1, MM_W = 2 };

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
