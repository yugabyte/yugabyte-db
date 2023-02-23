#ifndef MM_IOV_H
#define MM_IOV_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

/*
 * Structure for scatter/gather I/O
 * */

typedef struct mm_iov mm_iov_t;

struct mm_iov {
	mm_buf_t iov;
	int iov_count;
	int write_pos;
	mm_list_t msg_list;
};

static inline void mm_iov_init(mm_iov_t *iov)
{
	mm_buf_init(&iov->iov);
	mm_list_init(&iov->msg_list);
	iov->write_pos = 0;
	iov->iov_count = 0;
}

static inline void mm_iov_gc(mm_iov_t *iov)
{
	mm_list_t *i, *n;
	mm_list_foreach_safe(&iov->msg_list, i, n)
	{
		mm_msg_t *msg;
		msg = mm_container_of(i, mm_msg_t, link);
		machine_msg_free((machine_msg_t *)msg);
	}
	mm_list_init(&iov->msg_list);
}

static inline void mm_iov_free(mm_iov_t *iov)
{
	mm_buf_free(&iov->iov);
	mm_iov_gc(iov);
}

static inline void mm_iov_reset(mm_iov_t *iov)
{
	iov->write_pos = 0;
	iov->iov_count = 0;
	mm_buf_reset(&iov->iov);
	mm_iov_gc(iov);
}

__attribute__((hot)) static inline int
mm_iov_add_pointer(mm_iov_t *iov, void *pointer, int size)
{
	int rc;
	rc = mm_buf_ensure(&iov->iov, sizeof(struct iovec));
	if (rc == -1)
		return -1;
	struct iovec *iovec;
	iovec = (struct iovec *)iov->iov.pos;
	iovec->iov_base = pointer;
	iovec->iov_len = size;
	mm_buf_advance(&iov->iov, sizeof(struct iovec));
	iov->iov_count++;
	return 0;
}

static inline int mm_iov_add(mm_iov_t *iov, mm_msg_t *msg)
{
	int rc;
	rc = mm_iov_add_pointer(iov, msg->data.start, mm_buf_used(&msg->data));
	if (rc == -1)
		return -1;
	mm_list_append(&iov->msg_list, &msg->link);
	return 0;
}

static inline int mm_iov_pending(mm_iov_t *iov)
{
	return iov->iov_count > 0;
}

static inline struct iovec *mm_iov_pos(mm_iov_t *iov)
{
	struct iovec *iovec;
	iovec = (struct iovec *)iov->iov.start + iov->write_pos;
	return iovec;
}

static inline void mm_iov_advance(mm_iov_t *iov, int size)
{
	struct iovec *iovec = mm_iov_pos(iov);
	while (iov->iov_count > 0) {
		if (iovec->iov_len > (size_t)size) {
			iovec->iov_base = (char *)iovec->iov_base + size;
			iovec->iov_len -= size;
			break;
		}
		size -= iovec->iov_len;
		iovec++;
		iov->iov_count--;
		iov->write_pos++;
	}
	if (iov->iov_count == 0)
		mm_iov_reset(iov);
}

__attribute__((hot)) static inline int mm_iov_size_of(struct iovec *iov,
						      int count)
{
	int size = 0;
	while (count > 0) {
		size += iov->iov_len;
		iov++;
		count--;
	}
	return size;
}

__attribute__((hot)) static inline void mm_iovcpy(char *dest, struct iovec *iov,
						  int count)
{
	struct iovec *pos = iov;
	int pos_dest = 0;
	int n = count;
	while (n > 0) {
		memcpy(dest + pos_dest, pos->iov_base, pos->iov_len);
		pos_dest += pos->iov_len;
		pos++;
		n--;
	}
}

#endif /* MM_IOV_H */
