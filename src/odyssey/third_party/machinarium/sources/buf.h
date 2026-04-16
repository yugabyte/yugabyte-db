#ifndef MM_BUF_H
#define MM_BUF_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_buf mm_buf_t;

struct mm_buf {
	char *start;
	char *pos;
	char *end;
};

static inline void mm_buf_init(mm_buf_t *buf)
{
	buf->start = NULL;
	buf->pos = NULL;
	buf->end = NULL;
}

static inline void mm_buf_free(mm_buf_t *buf)
{
	if (buf->start == NULL)
		return;
	free(buf->start);
	buf->start = NULL;
	buf->pos = NULL;
	buf->end = NULL;
}

static inline int mm_buf_size(mm_buf_t *buf)
{
	return buf->end - buf->start;
}

static inline int mm_buf_used(mm_buf_t *buf)
{
	return buf->pos - buf->start;
}

static inline int mm_buf_unused(mm_buf_t *buf)
{
	return buf->end - buf->pos;
}

static inline void mm_buf_reset(mm_buf_t *buf)
{
	buf->pos = buf->start;
}

static inline int mm_buf_ensure(mm_buf_t *buf, int size)
{
	if (buf->end - buf->pos >= size)
		return 0;
	int sz = mm_buf_size(buf) * 2;
	int actual = mm_buf_used(buf) + size;
	if (actual > sz)
		sz = actual;
	char *p;
	p = realloc(buf->start, sz);
	if (p == NULL)
		return -1;
	buf->pos = p + (buf->pos - buf->start);
	buf->end = p + sz;
	buf->start = p;
	assert((buf->end - buf->pos) >= size);
	return 0;
}

static inline void mm_buf_advance(mm_buf_t *buf, int size)
{
	buf->pos += size;
}

static inline int mm_buf_add(mm_buf_t *buf, void *pointer, int size)
{
	int rc = mm_buf_ensure(buf, size);
	if (rc == -1)
		return -1;
	memcpy(buf->pos, pointer, size);
	mm_buf_advance(buf, size);
	return 0;
}

#endif /* MM_BUF_H */
