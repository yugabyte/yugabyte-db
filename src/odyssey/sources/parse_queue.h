#ifndef ODYSSEY_PARSE_QUEUE_H
#define ODYSSEY_PARSE_QUEUE_H
#include "circular_queue.h"
/*
 * Odyssey.
 *
 * Parse-queue entry types and typed wrappers over yb_od_circular_queue_t
 * for tracking outstanding parse-related operations per backend connection
 * (paired with ParseComplete / error drain).
 *
 * Each queue carries an `enabled` field, which denotes whether queue has been
 * enabled/disabled. When the enabled field is `false`, every enqueue/dequeue is
 * a no-op that returns success, and `empty()` returns true.
 */

typedef enum {
	YB_PARSE_QUEUE_SYNC,
	YB_PARSE_QUEUE_STMT_NAME,
} yb_od_parse_queue_kind_t;

typedef struct yb_od_parse_queue_entry {
	yb_od_parse_queue_kind_t kind;
	char *stmt_name;
} yb_od_parse_queue_entry_t;

/*
 * Typed wrapper over the generic circular queue and a per-queue enable
 * field.
 */
typedef struct yb_od_parse_queue {
	yb_od_circular_queue_t q;
	bool enabled;
} yb_od_parse_queue_t;

/* --- entry lifetime --- */

static inline void yb_od_parse_queue_entry_release(yb_od_parse_queue_entry_t *entry)
{
	switch (entry->kind) {
	case YB_PARSE_QUEUE_SYNC:
		break;
	case YB_PARSE_QUEUE_STMT_NAME:
		free(entry->stmt_name);
		entry->stmt_name = NULL;
		break;
	}
}

static inline void yb_od_parse_queue_entry_release_fn(void *elem)
{
	yb_od_parse_queue_entry_release((yb_od_parse_queue_entry_t *)elem);
}

/*
 * Initialise the queue header.  Defaults `enabled` to false. The intended
 * call site that flips `enabled` is yb_od_parse_queue_enable(). This is to ensure
 * that anyaccidental enqueue/dequeue with flag disabled is a safe no-op.
 */
static inline void yb_od_parse_queue_init(yb_od_parse_queue_t *q)
{
	yb_od_circular_queue_init(&q->q, sizeof(yb_od_parse_queue_entry_t));
	q->enabled = false;
}

/*
 * Enable or disable usage of the queue.  When enabled, enqueue/dequeue
 * operate on the queue normally; when disabled, they become no-ops that
 * return success.
 */
static inline void yb_od_parse_queue_enable(yb_od_parse_queue_t *q,
					    bool enabled)
{
	q->enabled = enabled;
}

static inline int yb_od_parse_queue_empty(yb_od_parse_queue_t *q)
{
	if (!q->enabled)
		return 1;
	return yb_od_circular_queue_empty(&q->q);
}

static inline int yb_od_parse_queue_count(const yb_od_parse_queue_t *q)
{
	if (!q->enabled)
		return 0;
	return yb_od_circular_queue_count(&q->q);
}

/*
 * Copies the front entry into *out.  Safe to use across queue reallocations;
 * the queue still owns stmt_name until dequeue frees it.
 * Returns 0 on success, -1 if the queue is empty (out is left untouched).
 *
 * When the queue is disabled, returns -1.  The only caller
 * (yb_drain_parse_queue_till_sync) guards with empty() first, so peek is
 * unreachable under the disabled snapshot.
 */
static inline int yb_od_parse_queue_peek(const yb_od_parse_queue_t *q,
					 yb_od_parse_queue_entry_t *out)
{
	if (!q->enabled)
		return -1;
	if (yb_od_circular_queue_empty(&q->q))
		return -1;
	*out = *(const yb_od_parse_queue_entry_t *)yb_od_circular_queue_peek(&q->q);
	return 0;
}

/*
 * Dequeue the front entry, freeing any owned heap state first.
 * Returns 0 on success, -1 if the queue is empty.
 *
 * When the queue is disabled, returns 0 (success no-op) so that callers
 * driven by upstream packets (e.g. KIWI_BE_PARSE_COMPLETE handling) do not
 * treat the disabled state as an error.
 */
static inline int yb_od_parse_queue_dequeue(yb_od_parse_queue_t *q)
{
	if (!q->enabled)
		return 0;
	if (yb_od_circular_queue_empty(&q->q))
		return -1;
	/* Cast away const: yb_od_circular_queue_peek() returns `const void *`
	 * (its read-only typed view), but here we own the storage and need
	 * a mutable pointer to free entry->stmt_name in _release() before
	 * the slot is dequeued. */
	yb_od_parse_queue_entry_t *entry =
		(yb_od_parse_queue_entry_t *)yb_od_circular_queue_peek(&q->q);
	yb_od_parse_queue_entry_release(entry);
	return yb_od_circular_queue_dequeue(&q->q);
}

/*
 * Free all storage.  Called unconditionally regardless of `enabled` field,
 * so a queue that was never enqueued into still leaves the underlying queue
 * in a clean state.
 */
static inline void yb_od_parse_queue_free(yb_od_parse_queue_t *q)
{
	yb_od_circular_queue_free(&q->q, yb_od_parse_queue_entry_release_fn);
}

/*
 * When the queue is disabled, enqueue helpers return 0 (success no-op) so
 * that the producer-side packet flow remains untouched and callers do not
 * have to special-case the kill switch.
 */
static inline int yb_od_parse_queue_enqueue_sync(yb_od_parse_queue_t *q)
{
	if (!q->enabled)
		return 0;
	yb_od_parse_queue_entry_t entry = {
		.kind = YB_PARSE_QUEUE_SYNC,
		.stmt_name = NULL,
	};
	return yb_od_circular_queue_enqueue(&q->q, &entry);
}

static inline int yb_od_parse_queue_enqueue_stmt_name(yb_od_parse_queue_t *q,
						   const char *stmt_name)
{
	if (!q->enabled)
		return 0;
	char *copy = strdup(stmt_name);
	if (copy == NULL)
		return -1;
	yb_od_parse_queue_entry_t entry = {
		.kind = YB_PARSE_QUEUE_STMT_NAME,
		.stmt_name = copy,
	};
	if (yb_od_circular_queue_enqueue(&q->q, &entry) == -1) {
		free(copy);
		return -1;
	}
	return 0;
}

#endif /* ODYSSEY_PARSE_QUEUE_H */
