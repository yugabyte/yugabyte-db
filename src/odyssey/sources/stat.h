#ifndef ODYSSEY_STAT_H
#define ODYSSEY_STAT_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#define QUANTILES_WINDOW 2
#define QUANTILES_COMPRESSION 100

typedef struct od_stat_state od_stat_state_t;
typedef struct od_stat od_stat_t;

struct od_stat_state {
	uint64_t query_time_start;
	uint64_t tx_time_start;
};

struct od_stat {
	bool enable_quantiles;
	uint8_t current_tdigest;

	od_atomic_u64_t count_query;
	od_atomic_u64_t count_tx;

	od_atomic_u64_t query_time;
	od_atomic_u64_t tx_time;

	od_atomic_u64_t recv_server;
	od_atomic_u64_t recv_client;
	od_atomic_u64_t count_parse;
	od_atomic_u64_t count_parse_reuse;

	td_histogram_t *transaction_hgram[QUANTILES_WINDOW];
	td_histogram_t *query_hgram[QUANTILES_WINDOW];

	// Time taken by a logical connection to acquire a physical connection.
	// i.e. wait time in the queue + time taken to find/create a physical connection
	od_atomic_u64_t wait_time;
};

static inline void od_stat_state_init(od_stat_state_t *state)
{
	memset(state, 0, sizeof(*state));
}

static inline void od_stat_init(od_stat_t *stat)
{
	memset(stat, 0, sizeof(*stat));
}

static inline void od_stat_free(od_stat_t *stat)
{
	for (size_t i = 0; i < QUANTILES_WINDOW; ++i) {
		td_free(stat->transaction_hgram[i]);
		td_free(stat->query_hgram[i]);
	}
}

static inline void od_stat_query_start(od_stat_state_t *state)
{
	if (!state->query_time_start)
		state->query_time_start = machine_time_us();

	if (!state->tx_time_start)
		state->tx_time_start = machine_time_us();
}

static inline void od_stat_parse(od_stat_t *stat)
{
	od_atomic_u64_inc(&stat->count_parse);
}

static inline void od_stat_parse_reuse(od_stat_t *stat)
{
	od_atomic_u64_inc(&stat->count_parse_reuse);
}

static inline void od_stat_query_end(od_stat_t *stat, od_stat_state_t *state,
				     int in_transaction, int64_t *query_time)
{
	int64_t diff;
	if (state->query_time_start) {
		diff = machine_time_us() - state->query_time_start;
		if (diff > 0) {
			*query_time = diff;
			od_atomic_u64_add(&stat->query_time, diff);
			od_atomic_u64_inc(&stat->count_query);
			if (stat->enable_quantiles) {
				td_add(stat->query_hgram[stat->current_tdigest],
				       diff, 1);
			}
		}
		state->query_time_start = 0;
	}

	if (in_transaction)
		return;

	if (state->tx_time_start) {
		diff = machine_time_us() - state->tx_time_start;
		if (diff > 0) {
			od_atomic_u64_add(&stat->tx_time, diff);
			od_atomic_u64_inc(&stat->count_tx);
			if (stat->enable_quantiles) {
				td_add(stat->transaction_hgram
					       [stat->current_tdigest],
				       diff, 1);
			}
		}
		state->tx_time_start = 0;
	}
}

static inline void od_stat_recv_server(od_stat_t *stat, uint64_t bytes)
{
	od_atomic_u64_add(&stat->recv_server, bytes);
}

static inline void od_stat_recv_client(od_stat_t *stat, uint64_t bytes)
{
	od_atomic_u64_add(&stat->recv_client, bytes);
}

static inline void od_stat_copy(od_stat_t *dst, od_stat_t *src)
{
	dst->count_query = od_atomic_u64_of(&src->count_query);
	dst->count_tx = od_atomic_u64_of(&src->count_tx);
	dst->query_time = od_atomic_u64_of(&src->query_time);
	dst->tx_time = od_atomic_u64_of(&src->tx_time);
	dst->recv_client = od_atomic_u64_of(&src->recv_client);
	dst->recv_server = od_atomic_u64_of(&src->recv_server);
	dst->count_parse = od_atomic_u64_of(&src->count_parse);
	dst->count_parse_reuse = od_atomic_u64_of(&src->count_parse_reuse);
	dst->wait_time = od_atomic_u64_of(&src->wait_time);
}

static inline void od_stat_sum(od_stat_t *sum, od_stat_t *stat)
{
	sum->count_query += od_atomic_u64_of(&stat->count_query);
	sum->count_tx += od_atomic_u64_of(&stat->count_tx);
	sum->query_time += od_atomic_u64_of(&stat->query_time);
	sum->tx_time += od_atomic_u64_of(&stat->tx_time);
	sum->recv_client += od_atomic_u64_of(&stat->recv_client);
	sum->recv_server += od_atomic_u64_of(&stat->recv_server);
	sum->count_parse += od_atomic_u64_of(&stat->count_parse);
	sum->count_parse_reuse += od_atomic_u64_of(&stat->count_parse_reuse);
	sum->wait_time += od_atomic_u64_of(&stat->wait_time);
}

static inline void od_stat_update_of(od_atomic_u64_t *prev,
				     od_atomic_u64_t *current)
{
	/* todo: this could be made more optimal */
	/* prev <= current */
	__atomic_store(prev, current, __ATOMIC_SEQ_CST);
}

static inline void od_stat_update(od_stat_t *dst, od_stat_t *stat)
{
	od_stat_update_of(&dst->count_query, &stat->count_query);
	od_stat_update_of(&dst->count_tx, &stat->count_tx);
	od_stat_update_of(&dst->query_time, &stat->query_time);
	od_stat_update_of(&dst->tx_time, &stat->tx_time);
	od_stat_update_of(&dst->recv_client, &stat->recv_client);
	od_stat_update_of(&dst->recv_server, &stat->recv_server);
	od_stat_update_of(&dst->count_parse, &stat->count_parse);
	od_stat_update_of(&dst->count_parse_reuse, &stat->count_parse_reuse);
	od_stat_update_of(&dst->wait_time, &stat->wait_time);
}

static inline void od_stat_average(od_stat_t *avg, od_stat_t *current,
				   od_stat_t *prev, uint64_t prev_time_us)
{
	const uint64_t interval_usec = 1000000;
	uint64_t interval_us;
	interval_us = machine_time_us() - prev_time_us;
	if (interval_us <= 0)
		return;

	uint64_t count_query;
	count_query = od_atomic_u64_of(&current->count_query) -
		      od_atomic_u64_of(&prev->count_query);

	uint64_t count_tx;
	count_tx = od_atomic_u64_of(&current->count_tx) -
		   od_atomic_u64_of(&prev->count_tx);

	uint64_t count_parse;
	count_parse = od_atomic_u64_of(&current->count_parse) -
		      od_atomic_u64_of(&prev->count_parse);

	uint64_t count_parse_reuse;
	count_parse_reuse = od_atomic_u64_of(&current->count_parse_reuse) -
			    od_atomic_u64_of(&prev->count_parse_reuse);

	avg->count_query = (count_query * interval_usec) / interval_us;
	avg->count_tx = (count_tx * interval_usec) / interval_us;
	avg->count_parse = (count_parse * interval_usec) / interval_us;
	avg->count_parse_reuse =
		(count_parse_reuse * interval_usec) / interval_us;

	if (count_query > 0) {
		avg->query_time = (od_atomic_u64_of(&current->query_time) -
				   od_atomic_u64_of(&prev->query_time)) /
				  count_query;
	}

	if (count_tx > 0) {
		avg->tx_time = (od_atomic_u64_of(&current->tx_time) -
				od_atomic_u64_of(&prev->tx_time)) /
			       count_tx;
		avg->wait_time = (od_atomic_u64_of(&current->wait_time) -
				od_atomic_u64_of(&prev->wait_time)) /
			       count_tx;
	}

	avg->recv_client = ((od_atomic_u64_of(&current->recv_client) -
			     od_atomic_u64_of(&prev->recv_client)) *
			    interval_usec) /
			   interval_us;

	avg->recv_server = ((od_atomic_u64_of(&current->recv_server) -
			     od_atomic_u64_of(&prev->recv_server)) *
			    interval_usec) /
			   interval_us;
}

#endif /* ODYSSEY_STAT_H */
