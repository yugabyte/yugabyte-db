#ifndef ODYSSEY_WORKER_H
#define ODYSSEY_WORKER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_worker od_worker_t;

struct od_worker {
	int64_t machine;
	int id;
	machine_channel_t *task_channel;
	uint64_t clients_processed;
	od_global_t *global;
};

void od_worker_init(od_worker_t *, od_global_t *, int);
int od_worker_start(od_worker_t *);

#endif /* ODYSSEY_WORKER_H */
