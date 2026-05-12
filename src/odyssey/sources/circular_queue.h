#ifndef ODYSSEY_CIRCULAR_QUEUE_H
#define ODYSSEY_CIRCULAR_QUEUE_H

/*
 * Odyssey.
 *
 * A generic growable circular FIFO queue. Elements are stored in an byte offset
 * array.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define OD_CIRCULAR_QUEUE_INITIAL_CAPACITY 8

/*
 * Growable circular FIFO buffer.
 *
 * All internal bookkeeping is kept in *bytes* rather than in element counts:
 *   - `head`     : byte offset of the front element into `data`.
 *   - `size`     : total bytes occupied by live elements.
 *   - `capacity` : total bytes allocated in `data`.
 *
 * NOTE: `size` and `capacity` are byte quantities.
 *
 *   data:  [ slot_0 | slot_1 | ... ]
 *            ^ data + head           ^ tail (next enqueue)
 *   data + capacity is one-past-the-end of the buffer.
 */
typedef struct {
	char *data;        /* backing byte buffer; capacity bytes long */
	size_t head;       /* byte offset of front element;
			    * in [0, capacity) when size > 0 */
	size_t size;       /* bytes currently live; in [0, capacity] */
	size_t capacity;   /* total bytes allocated in data */
	size_t elem_size;  /* byte size of a single element */
} yb_od_circular_queue_t;

/*
 * Initialize a queue header by initializing all fields to 0 and then records `elem_size`.
 * The first enqueue will lazy-allocate via grow().
 */
static inline void yb_od_circular_queue_init(yb_od_circular_queue_t *q,
					     size_t elem_size)
{
	q->data = NULL;
	q->head = 0;
	q->size = 0;
	q->capacity = 0;
	q->elem_size = elem_size;
}

static inline int yb_od_circular_queue_empty(const yb_od_circular_queue_t *q)
{
	return q->size == 0;
}

/*
 * Element-count accessors.  These divide by elem_size, so they are *not*
 * intended for hot-path use; reach for them in tests, diagnostics, or
 * external code that wants element-level semantics.
 */
static inline int yb_od_circular_queue_count(const yb_od_circular_queue_t *q)
{
	return (int)(q->size / q->elem_size);
}

static inline const void *yb_od_circular_queue_peek(const yb_od_circular_queue_t *q)
{
	if (q->size == 0)
		return NULL;
	return q->data + q->head;
}

static inline int yb_od_circular_queue_grow(yb_od_circular_queue_t *q)
{
	/*
	 * Lazy initial allocation
	 */
	if (q->data == NULL) {
		size_t initial_cap =
			(size_t)OD_CIRCULAR_QUEUE_INITIAL_CAPACITY *
			q->elem_size;
		q->data = (char *)malloc(initial_cap);
		if (q->data == NULL)
			return -1;
		q->capacity = initial_cap;
		q->head = 0;
		q->size = 0;
		return 0;
	}

	if (q->size < q->capacity)
		return 0;

	size_t new_capacity = q->capacity * 2;
	char *new_data = (char *)malloc(new_capacity);
	if (new_data == NULL)
		return -1;

	/* Linearize content into the new buffer: head..end first,
	 * then start..head (which may be empty if head == 0). */
	size_t front_bytes = q->capacity - q->head;
	size_t back_bytes  = q->head;
	memcpy(new_data, q->data + q->head, front_bytes);
	memcpy(new_data + front_bytes, q->data, back_bytes);

	free(q->data);
	q->data = new_data;
	q->capacity = new_capacity;
	q->head = 0;
	return 0;
}

/*
 * Grow the queue if needed and copy elem into the new tail slot.
 * Returns 0 on success, -1 on allocation failure.
 */
static inline int yb_od_circular_queue_enqueue(yb_od_circular_queue_t *q,
					       const void *elem)
{
	if (yb_od_circular_queue_grow(q) == -1)
		return -1;
	/* Tail = head advanced by `size` bytes, wrapped if it overshoots.
	 * Post-grow we know size < capacity, so the sum is strictly less
	 * than 2 * capacity and one subtract is sufficient. */
	size_t tail = q->head + q->size;
	if (tail >= q->capacity)
		tail -= q->capacity;
	memcpy(q->data + tail, elem, q->elem_size);
	q->size += q->elem_size;
	return 0;
}

static inline int yb_od_circular_queue_dequeue(yb_od_circular_queue_t *q)
{
	if (q->size == 0)
		return -1;
	q->head += q->elem_size;
	/*
	 * `capacity` is always a multiple of `elem_size` (initial alloc is
	 * OD_CIRCULAR_QUEUE_INITIAL_CAPACITY * elem_size, grow() doubles it),
	 * and the pre-increment head was in [0, capacity) on an elem_size
	 * boundary.  So after the bump head lies in (0, capacity], and the
	 * only value that needs wrapping is exactly `capacity` -- equality
	 * suffices, no `>=` or modulo required.
	 */
	if (q->head == q->capacity)
		q->head = 0;
	q->size -= q->elem_size;
	return 0;
}

/*
 * Free all storage.  If release_fn is non-NULL it is called once for
 * each element still in the queue before the backing buffer is freed.
 */
static inline void yb_od_circular_queue_free(yb_od_circular_queue_t *q,
					     void (*release_fn)(void *))
{
	if (release_fn && q->size > 0) {
		size_t off = q->head;
		size_t walked = 0;
		while (walked < q->size) {
			release_fn(q->data + off);
			off += q->elem_size;
			if (off == q->capacity)
				off = 0;
			walked += q->elem_size;
		}
	}
	free(q->data);
	q->data = NULL;
	q->head = 0;
	q->size = 0;
	q->capacity = 0;
}

#endif /* ODYSSEY_CIRCULAR_QUEUE_H */
