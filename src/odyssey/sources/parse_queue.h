#ifndef ODYSSEY_PARSE_QUEUE_H
#define ODYSSEY_PARSE_QUEUE_H
#include "circular_queue.h"
/*
 * Odyssey.
 *
 * Parse-queue entry types and typed wrappers over yb_od_circular_queue_t
 * for tracking outstanding operations per backend connection.
 */

typedef enum {
	YB_PARSE_QUEUE_SYNC,
	YB_PARSE_QUEUE_NAMED_PARSE,
	YB_PARSE_QUEUE_NAMED_REDEPLOY,
	YB_PARSE_QUEUE_NAMED_CLOSE,
	YB_PARSE_QUEUE_PORTAL_CLOSE,
	YB_PARSE_QUEUE_UNNAMED_PARSE,
	YB_PARSE_QUEUE_UNNAMED_CLOSE,
	YB_PARSE_QUEUE_QUERY,
} yb_od_parse_queue_kind_t;

typedef struct yb_od_parse_queue_entry {
	yb_od_parse_queue_kind_t kind;
	char *stmt_name;
	void *desc;
	size_t desc_len;
	od_id_t prev_unnamed_client_id;
} yb_od_parse_queue_entry_t;

typedef struct yb_od_parse_queue {
	yb_od_circular_queue_t q;
} yb_od_parse_queue_t;

/* --- entry lifetime --- */

static inline void yb_od_parse_queue_entry_release(yb_od_parse_queue_entry_t *entry)
{
	free(entry->stmt_name);
	entry->stmt_name = NULL;
	free(entry->desc);
	entry->desc = NULL;
	entry->desc_len = 0;
}

static inline void yb_od_parse_queue_entry_release_fn(void *elem)
{
	yb_od_parse_queue_entry_release((yb_od_parse_queue_entry_t *)elem);
}

static inline void yb_od_parse_queue_init(yb_od_parse_queue_t *q)
{
	yb_od_circular_queue_init(&q->q, sizeof(yb_od_parse_queue_entry_t));
}

static inline int yb_od_parse_queue_empty(yb_od_parse_queue_t *q)
{
	return yb_od_circular_queue_empty(&q->q);
}

static inline int yb_od_parse_queue_count(const yb_od_parse_queue_t *q)
{
	return yb_od_circular_queue_count(&q->q);
}

/*
 * Copies the front entry into *out.  Safe to use across queue reallocations.
 * Returns 0 on success, -1 if the queue is empty (out is left untouched).
 */
static inline int yb_od_parse_queue_peek(const yb_od_parse_queue_t *q,
					 yb_od_parse_queue_entry_t *out)
{
	if (yb_od_circular_queue_empty(&q->q))
		return -1;
	*out = *(const yb_od_parse_queue_entry_t *)yb_od_circular_queue_peek(&q->q);
	return 0;
}

/*
 * Copies the most recently enqueued (tail) entry into *out.  Mirrors
 * yb_od_parse_queue_peek but reads from the tail instead of the front.
 * Returns 0 on success, -1 if the queue is empty.
 */
static inline int yb_od_parse_queue_peek_last(const yb_od_parse_queue_t *q,
					      yb_od_parse_queue_entry_t *out)
{
	const void *elem = yb_od_circular_queue_peek_last(&q->q);
	if (elem == NULL)
		return -1;
	*out = *(const yb_od_parse_queue_entry_t *)elem;
	return 0;
}

/*
 * Remove the most recently enqueued (tail) entry, freeing any owned heap
 * state first.
 * Returns 0 on success, -1 if the queue is empty.
 */
static inline int yb_od_parse_queue_remove_last(yb_od_parse_queue_t *q)
{
	if (yb_od_circular_queue_empty(&q->q))
		return -1;
	yb_od_parse_queue_entry_t *entry =
		(yb_od_parse_queue_entry_t *)yb_od_circular_queue_peek_last(&q->q);
	yb_od_parse_queue_entry_release(entry);
	return yb_od_circular_queue_remove_last(&q->q);
}

/*
 * Dequeue the front entry, freeing any owned heap state first.
 * Returns 0 on success, -1 if the queue is empty.
 */
static inline int yb_od_parse_queue_dequeue(yb_od_parse_queue_t *q)
{
	if (yb_od_circular_queue_empty(&q->q))
		return -1;
	/* Cast away const: yb_od_circular_queue_peek() returns `const void *`
	 * (its read-only typed view), but here we own the storage and need
	 * a mutable pointer for _release() before the slot is dequeued. */
	yb_od_parse_queue_entry_t *entry =
		(yb_od_parse_queue_entry_t *)yb_od_circular_queue_peek(&q->q);
	yb_od_parse_queue_entry_release(entry);
	return yb_od_circular_queue_dequeue(&q->q);
}

static inline int yb_od_parse_queue_dequeue_take(yb_od_parse_queue_t *q,
						 yb_od_parse_queue_entry_t *out)
{
	if (yb_od_circular_queue_empty(&q->q))
		return -1;
	*out = *(const yb_od_parse_queue_entry_t *)yb_od_circular_queue_peek(
		&q->q);
	return yb_od_circular_queue_dequeue(&q->q);
}

static inline void yb_od_parse_queue_free(yb_od_parse_queue_t *q)
{
	yb_od_circular_queue_free(&q->q, yb_od_parse_queue_entry_release_fn);
}

static inline int yb_od_parse_queue_enqueue_entry(yb_od_parse_queue_t *q,
						  yb_od_parse_queue_kind_t kind,
						  const char *stmt_name,
						  const void *desc,
						  size_t desc_len,
						  const od_id_t *prev_id)
{

	yb_od_parse_queue_entry_t entry;
	memset(&entry, 0, sizeof(entry));
	entry.kind = kind;

	if (stmt_name != NULL) {
		entry.stmt_name = strdup(stmt_name);
		if (entry.stmt_name == NULL)
			return -1;
	}

	if (desc != NULL && desc_len > 0) {
		entry.desc = malloc(desc_len);
		if (entry.desc == NULL) {
			free(entry.stmt_name);
			return -1;
		}
		memcpy(entry.desc, desc, desc_len);
		entry.desc_len = desc_len;
	}

	if (prev_id != NULL)
		entry.prev_unnamed_client_id = *prev_id;

	if (yb_od_circular_queue_enqueue(&q->q, &entry) == -1) {
		free(entry.stmt_name);
		free(entry.desc);
		return -1;
	}
	return 0;
}

static inline int yb_od_parse_queue_enqueue_sync(yb_od_parse_queue_t *q)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_SYNC, NULL,
					       NULL, 0, NULL);
}

static inline int yb_od_parse_queue_enqueue_named_parse(yb_od_parse_queue_t *q,
							const char *stmt_name,
							const void *prev_desc,
							size_t prev_desc_len)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_NAMED_PARSE,
					       stmt_name, prev_desc,
					       prev_desc_len, NULL);
}

static inline int
yb_od_parse_queue_enqueue_named_redeploy(yb_od_parse_queue_t *q)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_NAMED_REDEPLOY,
					       NULL, NULL, 0, NULL);
}

static inline int yb_od_parse_queue_enqueue_named_close(yb_od_parse_queue_t *q,
							const char *stmt_name,
							const void *desc,
							size_t desc_len)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_NAMED_CLOSE,
					       stmt_name, desc, desc_len, NULL);
}

static inline int yb_od_parse_queue_enqueue_portal_close(yb_od_parse_queue_t *q)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_PORTAL_CLOSE,
					       NULL, NULL, 0, NULL);
}

static inline int
yb_od_parse_queue_enqueue_unnamed_parse(yb_od_parse_queue_t *q,
					const od_id_t *prev_id)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_UNNAMED_PARSE,
					       NULL, NULL, 0, prev_id);
}

static inline int
yb_od_parse_queue_enqueue_unnamed_close(yb_od_parse_queue_t *q,
					const od_id_t *prev_id)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_UNNAMED_CLOSE,
					       NULL, NULL, 0, prev_id);
}

static inline int yb_od_parse_queue_enqueue_query(yb_od_parse_queue_t *q,
						  const od_id_t *prev_id)
{
	return yb_od_parse_queue_enqueue_entry(q, YB_PARSE_QUEUE_QUERY, NULL,
					       NULL, 0, prev_id);
}

#endif /* ODYSSEY_PARSE_QUEUE_H */
