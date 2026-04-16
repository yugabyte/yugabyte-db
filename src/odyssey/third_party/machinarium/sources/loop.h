#ifndef MM_LOOP_H
#define MM_LOOP_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_loop mm_loop_t;

struct mm_loop {
	mm_clock_t clock;
	mm_idle_t idle;
	mm_poll_t *poll;
};

int mm_loop_init(mm_loop_t *);
int mm_loop_shutdown(mm_loop_t *);
int mm_loop_step(mm_loop_t *);

static inline void mm_loop_set_idle(mm_loop_t *loop, mm_idle_callback_t cb,
				    void *arg)
{
	loop->idle.callback = cb;
	loop->idle.arg = arg;
}

static inline int mm_loop_add(mm_loop_t *loop, mm_fd_t *fd, int mask)
{
	return loop->poll->iface->add(loop->poll, fd, mask);
}

static inline int mm_loop_delete(mm_loop_t *loop, mm_fd_t *fd)
{
	return loop->poll->iface->del(loop->poll, fd);
}

static inline int mm_loop_read(mm_loop_t *loop, mm_fd_t *fd,
			       mm_fd_callback_t on_read, void *arg)
{
	return loop->poll->iface->read(loop->poll, fd, on_read, arg, 1);
}

static inline int mm_loop_read_stop(mm_loop_t *loop, mm_fd_t *fd)
{
	return loop->poll->iface->read(loop->poll, fd, NULL, NULL, 0);
}

static inline int mm_loop_write(mm_loop_t *loop, mm_fd_t *fd,
				mm_fd_callback_t on_write, void *arg)
{
	return loop->poll->iface->write(loop->poll, fd, on_write, arg, 1);
}

static inline int mm_loop_write_stop(mm_loop_t *loop, mm_fd_t *fd)
{
	return loop->poll->iface->write(loop->poll, fd, NULL, NULL, 0);
}

static inline int mm_loop_read_write(mm_loop_t *loop, mm_fd_t *fd,
				     mm_fd_callback_t on_event, void *arg)
{
	return loop->poll->iface->read_write(loop->poll, fd, on_event, arg, 1);
}

static inline int mm_loop_read_write_stop(mm_loop_t *loop, mm_fd_t *fd)
{
	return loop->poll->iface->read_write(loop->poll, fd, NULL, NULL, 0);
}

#endif /* MM_LOOP_H */
