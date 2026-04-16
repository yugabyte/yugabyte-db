#ifndef MM_POLL_H
#define MM_POLL_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_pollif mm_pollif_t;
typedef struct mm_poll mm_poll_t;

struct mm_pollif {
	char *name;
	mm_poll_t *(*create)(void);
	void (*free)(mm_poll_t *);
	int (*shutdown)(mm_poll_t *);
	int (*step)(mm_poll_t *, int);
	int (*add)(mm_poll_t *, mm_fd_t *, int);
	int (*read)(mm_poll_t *, mm_fd_t *, mm_fd_callback_t, void *, int);
	int (*write)(mm_poll_t *, mm_fd_t *, mm_fd_callback_t, void *, int);
	int (*read_write)(mm_poll_t *, mm_fd_t *, mm_fd_callback_t, void *,
			  int);
	int (*del)(mm_poll_t *, mm_fd_t *);
};

struct mm_poll {
	mm_pollif_t *iface;
};

#endif /* MM_POLL_H */
