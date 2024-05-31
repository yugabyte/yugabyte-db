
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

typedef struct mm_epoll_t mm_epoll_t;

struct mm_epoll_t {
	mm_poll_t poll;
	int fd;
	struct epoll_event *list;
	int size;
	int count;
};

static mm_poll_t *mm_epoll_create(void)
{
	mm_epoll_t *epoll;
	epoll = malloc(sizeof(mm_epoll_t));
	if (epoll == NULL)
		return NULL;
	epoll->poll.iface = &mm_epoll_if;
	epoll->count = 0;
	epoll->size = 1024;
	int size = sizeof(struct epoll_event) * epoll->size;
	epoll->list = malloc(size);
	if (epoll->list == NULL) {
		free(epoll);
		return NULL;
	}
	memset(epoll->list, 0, size);
	epoll->fd = epoll_create(epoll->size);
	if (epoll->fd == -1) {
		free(epoll->list);
		free(epoll);
		return NULL;
	}
	return &epoll->poll;
}

static void mm_epoll_free(mm_poll_t *poll)
{
	mm_epoll_t *epoll = (mm_epoll_t *)poll;
	if (epoll->list)
		free(epoll->list);
	free(poll);
}

static int mm_epoll_shutdown(mm_poll_t *poll)
{
	mm_epoll_t *epoll = (mm_epoll_t *)poll;
	if (epoll->fd != -1) {
		close(epoll->fd);
		epoll->fd = -1;
	}
	return 0;
}

static int mm_epoll_step(mm_poll_t *poll, int timeout)
{
	mm_epoll_t *epoll = (mm_epoll_t *)poll;
	if (epoll->count == 0)
		return 0;
	int count;
	count = epoll_wait(epoll->fd, epoll->list, epoll->count, timeout);
	if (count <= 0)
		return 0;
	int i = 0;
	while (i < count) {
		struct epoll_event *ev = &epoll->list[i];
		mm_fd_t *fd = ev->data.ptr;
		if (fd->on_read) {
			if (ev->events & EPOLLIN)
				fd->on_read(fd);
		}
		if (fd->on_write) {
			if (ev->events & EPOLLOUT || ev->events & EPOLLERR ||
			    ev->events & EPOLLHUP) {
				fd->on_write(fd);
			}
		}
		i++;
	}
	return count;
}

static int mm_epoll_add(mm_poll_t *poll, mm_fd_t *fd, int mask)
{
	mm_epoll_t *epoll = (mm_epoll_t *)poll;
	if ((epoll->count + 1) > epoll->size) {
		int size = epoll->size * 2;
		void *ptr =
			realloc(epoll->list, sizeof(struct epoll_event) * size);
		if (ptr == NULL)
			return -1;
		epoll->list = ptr;
		epoll->size = size;
	}
	struct epoll_event ev;
	ev.events = 0;
	fd->mask = mask;
	if (fd->mask & MM_R)
		ev.events |= EPOLLIN;
	if (fd->mask & MM_W)
		ev.events |= EPOLLOUT;
	ev.data.ptr = fd;
	int rc = epoll_ctl(epoll->fd, EPOLL_CTL_ADD, fd->fd, &ev);
	if (rc == -1)
		return -1;
	epoll->count++;
	return 0;
}

static inline int mm_epoll_modify(mm_poll_t *poll, mm_fd_t *fd, int mask)
{
	mm_epoll_t *epoll = (mm_epoll_t *)poll;
	struct epoll_event ev;
	ev.events = 0;
	if (mask & MM_R)
		ev.events |= EPOLLIN;
	if (mask & MM_W)
		ev.events |= EPOLLOUT;
	ev.data.ptr = fd;
	int rc = epoll_ctl(epoll->fd, EPOLL_CTL_MOD, fd->fd, &ev);
	if (rc == -1)
		return -1;
	fd->mask = mask;
	return 0;
}

static int mm_epoll_read(mm_poll_t *poll, mm_fd_t *fd, mm_fd_callback_t on_read,
			 void *arg, int enable)
{
	int mask = fd->mask;
	if (enable)
		mask |= MM_R;
	else
		mask &= ~MM_R;
	fd->on_read = on_read;
	fd->on_read_arg = arg;
	if (mask == fd->mask)
		return 0;
	return mm_epoll_modify(poll, fd, mask);
}

static int mm_epoll_write(mm_poll_t *poll, mm_fd_t *fd,
			  mm_fd_callback_t on_write, void *arg, int enable)
{
	int mask = fd->mask;
	if (enable)
		mask |= MM_W;
	else
		mask &= ~MM_W;
	fd->on_write = on_write;
	fd->on_write_arg = arg;
	if (mask == fd->mask)
		return 0;
	return mm_epoll_modify(poll, fd, mask);
}

static int mm_epoll_read_write(mm_poll_t *poll, mm_fd_t *fd,
			       mm_fd_callback_t on_event, void *arg, int enable)
{
	int mask = fd->mask;
	if (enable)
		mask |= MM_W | MM_R;
	else
		mask &= ~MM_W | MM_R;
	fd->on_write = on_event;
	fd->on_write_arg = arg;
	fd->on_read = on_event;
	fd->on_read_arg = arg;
	if (mask == fd->mask)
		return 0;
	return mm_epoll_modify(poll, fd, mask);
}

static int mm_epoll_del(mm_poll_t *poll, mm_fd_t *fd)
{
	mm_epoll_t *epoll = (mm_epoll_t *)poll;
	struct epoll_event ev;
	ev.events = 0;
	if (fd->mask & MM_R)
		ev.events |= EPOLLIN;
	if (fd->mask & MM_W)
		ev.events |= EPOLLOUT;
	ev.data.ptr = fd;
	fd->mask = 0;
	fd->on_write = NULL;
	fd->on_write_arg = NULL;
	fd->on_read = NULL;
	fd->on_read_arg = NULL;
	epoll->count--;
	assert(epoll->count >= 0);
	return epoll_ctl(epoll->fd, EPOLL_CTL_DEL, fd->fd, &ev);
}

mm_pollif_t mm_epoll_if = { .name = "epoll",
			    .create = mm_epoll_create,
			    .free = mm_epoll_free,
			    .shutdown = mm_epoll_shutdown,
			    .step = mm_epoll_step,
			    .add = mm_epoll_add,
			    .read = mm_epoll_read,
			    .write = mm_epoll_write,
			    .read_write = mm_epoll_read_write,
			    .del = mm_epoll_del };
